/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2010 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * outelf64.c   output routines for the Netwide Assembler to produce
 *              ELF64 (x86_64 of course) object file format
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "nasm.h"
#include "nasmlib.h"
#include "saa.h"
#include "raa.h"
#include "stdscan.h"
#include "eval.h"
#include "output/outform.h"
#include "output/outlib.h"
#include "rbtree.h"

#include "output/dwarf.h"
#include "output/stabs.h"
#include "output/outelf.h"

#ifdef OF_ELF64

/*
 * Relocation types.
 */
struct Reloc {
    struct Reloc *next;
    int64_t address;            /* relative to _start_ of section */
    int64_t symbol;             /* symbol index */
    int64_t offset;             /* symbol addend */
    int type;                   /* type of relocation */
};

struct Symbol {
    struct rbtree symv;         /* symbol value and rbtree of globals */
    int32_t strpos;             /* string table position of name */
    int32_t section;            /* section ID of the symbol */
    int type;                   /* symbol type */
    int other;                  /* symbol visibility */
    int32_t size;               /* size of symbol */
    int32_t globnum;            /* symbol table offset if global */
    struct Symbol *nextfwd;     /* list of unresolved-size symbols */
    char *name;                 /* used temporarily if in above list */
};

struct Section {
    struct SAA *data;
    uint64_t len, size;
    uint32_t nrelocs;
    int32_t index;              /* index into sects array */
    int type;                   /* SHT_PROGBITS or SHT_NOBITS */
    uint64_t align;             /* alignment: power of two */
    uint64_t flags;             /* section flags */
    char *name;
    struct SAA *rel;
    uint64_t rellen;
    struct Reloc *head, **tail;
    struct rbtree *gsyms;       /* global symbols in section */
};

#define SECT_DELTA 32
static struct Section **sects;
static int nsects, sectlen;

#define SHSTR_DELTA 256
static char *shstrtab;
static int shstrtablen, shstrtabsize;

static struct SAA *syms;
static uint32_t nlocals, nglobs, ndebugs; /* Symbol counts */

static int32_t def_seg;

static struct RAA *bsym;

static struct SAA *strs;
static uint32_t strslen;

static struct Symbol *fwds;

static char elf_module[FILENAME_MAX];

static uint8_t elf_osabi = 0;   /* Default OSABI = 0 (System V or Linux) */
static uint8_t elf_abiver = 0;  /* Current ABI version */

extern struct ofmt of_elf64;

static struct ELF_SECTDATA {
    void *data;
    int64_t len;
    bool is_saa;
} *elf_sects;
static int elf_nsect, nsections;
static int64_t elf_foffs;

static void elf_write(void);
static void elf_sect_write(struct Section *, const void *, size_t);
static void elf_sect_writeaddr(struct Section *, int64_t, size_t);
static void elf_section_header(int, int, uint64_t, void *, bool, uint64_t, int, int,
                               int, int);
static void elf_write_sections(void);
static struct SAA *elf_build_symtab(int32_t *, int32_t *);
static struct SAA *elf_build_reltab(uint64_t *, struct Reloc *);
static void add_sectname(char *, char *);

struct erel {
    int offset, info;
};

struct symlininfo {
    int offset;
    int section;                /* index into sects[] */
    int segto;                  /* internal section number */
    char *name;                 /* shallow-copied pointer of section name */
};

struct linelist {
    struct linelist *next;
    struct linelist *last;
    struct symlininfo info;
    char *filename;
    int line;
};

struct sectlist {
    struct SAA *psaa;
    int section;
    int line;
    int offset;
    int file;
    struct sectlist *next;
    struct sectlist *last;
};

/* common debug variables */
static int currentline = 1;
static int debug_immcall = 0;

/* stabs debug variables */
static struct linelist *stabslines = 0;
static int numlinestabs = 0;
static char *stabs_filename = 0;
static int symtabsection;
static uint8_t *stabbuf = 0, *stabstrbuf = 0, *stabrelbuf = 0;
static int stablen, stabstrlen, stabrellen;

/* dwarf debug variables */
static struct linelist *dwarf_flist = 0, *dwarf_clist = 0, *dwarf_elist = 0;
static struct sectlist *dwarf_fsect = 0, *dwarf_csect = 0, *dwarf_esect = 0;
static int dwarf_numfiles = 0, dwarf_nsections;
static uint8_t *arangesbuf = 0, *arangesrelbuf = 0, *pubnamesbuf = 0, *infobuf = 0,  *inforelbuf = 0,
               *abbrevbuf = 0, *linebuf = 0, *linerelbuf = 0, *framebuf = 0, *locbuf = 0;
static int8_t line_base = -5, line_range = 14, opcode_base = 13;
static int arangeslen, arangesrellen, pubnameslen, infolen, inforellen,
           abbrevlen, linelen, linerellen, framelen, loclen;
static int64_t dwarf_infosym, dwarf_abbrevsym, dwarf_linesym;


static struct dfmt df_dwarf;
static struct dfmt df_stabs;
static struct Symbol *lastsym;

/* common debugging routines */
static void debug64_typevalue(int32_t);
static void debug64_deflabel(char *, int32_t, int64_t, int, char *);
static void debug64_directive(const char *, const char *);

/* stabs debugging routines */
static void stabs64_linenum(const char *filename, int32_t linenumber, int32_t);
static void stabs64_output(int, void *);
static void stabs64_generate(void);
static void stabs64_cleanup(void);

/* dwarf debugging routines */
static void dwarf64_init(void);
static void dwarf64_linenum(const char *filename, int32_t linenumber, int32_t);
static void dwarf64_output(int, void *);
static void dwarf64_generate(void);
static void dwarf64_cleanup(void);
static void dwarf64_findfile(const char *);
static void dwarf64_findsect(const int);

/*
 * Special section numbers which are used to define ELF special
 * symbols, which can be used with WRT to provide PIC relocation
 * types.
 */
static int32_t elf_gotpc_sect, elf_gotoff_sect;
static int32_t elf_got_sect, elf_plt_sect;
static int32_t elf_sym_sect;
static int32_t elf_gottpoff_sect;

static void elf_init(void)
{
    maxbits = 64;
    sects = NULL;
    nsects = sectlen = 0;
    syms = saa_init((int32_t)sizeof(struct Symbol));
    nlocals = nglobs = ndebugs = 0;
    bsym = raa_init();
    strs = saa_init(1L);
    saa_wbytes(strs, "\0", 1L);
    saa_wbytes(strs, elf_module, strlen(elf_module)+1);
    strslen = 2 + strlen(elf_module);
    shstrtab = NULL;
    shstrtablen = shstrtabsize = 0;;
    add_sectname("", "");

    fwds = NULL;

    elf_gotpc_sect = seg_alloc();
    define_label("..gotpc", elf_gotpc_sect + 1, 0L, NULL, false, false);
    elf_gotoff_sect = seg_alloc();
    define_label("..gotoff", elf_gotoff_sect + 1, 0L, NULL, false, false);
    elf_got_sect = seg_alloc();
    define_label("..got", elf_got_sect + 1, 0L, NULL, false, false);
    elf_plt_sect = seg_alloc();
    define_label("..plt", elf_plt_sect + 1, 0L, NULL, false, false);
    elf_sym_sect = seg_alloc();
    define_label("..sym", elf_sym_sect + 1, 0L, NULL, false, false);
    elf_gottpoff_sect = seg_alloc();
    define_label("..gottpoff", elf_gottpoff_sect + 1, 0L, NULL, false, false);

    def_seg = seg_alloc();

}

static void elf_cleanup(int debuginfo)
{
    struct Reloc *r;
    int i;

    (void)debuginfo;

    elf_write();
    for (i = 0; i < nsects; i++) {
        if (sects[i]->type != SHT_NOBITS)
            saa_free(sects[i]->data);
        if (sects[i]->head)
            saa_free(sects[i]->rel);
        while (sects[i]->head) {
            r = sects[i]->head;
            sects[i]->head = sects[i]->head->next;
            nasm_free(r);
        }
    }
    nasm_free(sects);
    saa_free(syms);
    raa_free(bsym);
    saa_free(strs);
    if (of_elf64.current_dfmt) {
        of_elf64.current_dfmt->cleanup();
    }
}

/* add entry to the elf .shstrtab section */
static void add_sectname(char *firsthalf, char *secondhalf)
{
    int len = strlen(firsthalf) + strlen(secondhalf);
    while (shstrtablen + len + 1 > shstrtabsize)
        shstrtab = nasm_realloc(shstrtab, (shstrtabsize += SHSTR_DELTA));
    strcpy(shstrtab + shstrtablen, firsthalf);
    strcat(shstrtab + shstrtablen, secondhalf);
    shstrtablen += len + 1;
}

static int elf_make_section(char *name, int type, int flags, int align)
{
    struct Section *s;

    s = nasm_zalloc(sizeof(*s));

    if (type != SHT_NOBITS)
        s->data = saa_init(1L);
    s->tail = &s->head;
    if (!strcmp(name, ".text"))
        s->index = def_seg;
    else
        s->index = seg_alloc();
    add_sectname("", name);

    s->name     = nasm_strdup(name);
    s->type     = type;
    s->flags    = flags;
    s->align    = align;

    if (nsects >= sectlen)
        sects = nasm_realloc(sects, (sectlen += SECT_DELTA) * sizeof(*sects));
    sects[nsects++] = s;

    return nsects - 1;
}

static int32_t elf_section_names(char *name, int pass, int *bits)
{
    char *p;
    uint32_t flags, flags_and, flags_or;
    uint64_t align;
    int type, i;

    /*
     * Default is 64 bits.
     */
    if (!name) {
        *bits = 64;
        return def_seg;
    }

    p = nasm_skip_word(name);
    if (*p)
        *p++ = '\0';
    flags_and = flags_or = type = align = 0;

    section_attrib(name, p, pass, &flags_and,
                   &flags_or, &align, &type);

    if (!strcmp(name, ".shstrtab") ||
        !strcmp(name, ".symtab") ||
        !strcmp(name, ".strtab")) {
        nasm_error(ERR_NONFATAL, "attempt to redefine reserved section"
              "name `%s'", name);
        return NO_SEG;
    }

    for (i = 0; i < nsects; i++)
        if (!strcmp(name, sects[i]->name))
            break;
    if (i == nsects) {
        const struct elf_known_section *ks = elf_known_sections;

        while (ks->name) {
            if (!strcmp(name, ks->name))
                break;
            ks++;
        }

        type = type ? type : ks->type;
        align = align ? align : ks->align;
        flags = (ks->flags & ~flags_and) | flags_or;

        i = elf_make_section(name, type, flags, align);
    } else if (pass == 1) {
          if ((type && sects[i]->type != type)
             || (align && sects[i]->align != align)
             || (flags_and && ((sects[i]->flags & flags_and) != flags_or)))
            nasm_error(ERR_WARNING, "incompatible section attributes ignored on"
                  " redeclaration of section `%s'", name);
    }

    return sects[i]->index;
}

static void elf_deflabel(char *name, int32_t segment, int64_t offset,
                         int is_global, char *special)
{
    int pos = strslen;
    struct Symbol *sym;
    bool special_used = false;

#if defined(DEBUG) && DEBUG>2
    nasm_error(ERR_DEBUG,
            " elf_deflabel: %s, seg=%"PRIx32", off=%"PRIx64", is_global=%d, %s\n",
            name, segment, offset, is_global, special);
#endif
    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        /*
         * This is a NASM special symbol. We never allow it into
         * the ELF symbol table, even if it's a valid one. If it
         * _isn't_ a valid one, we should barf immediately.
         */
        if (strcmp(name, "..gotpc") && strcmp(name, "..gotoff") &&
            strcmp(name, "..got") && strcmp(name, "..plt") &&
            strcmp(name, "..sym") && strcmp(name, "..gottpoff"))
            nasm_error(ERR_NONFATAL, "unrecognised special symbol `%s'", name);
        return;
    }

    if (is_global == 3) {
        struct Symbol **s;
        /*
         * Fix up a forward-reference symbol size from the first
         * pass.
         */
        for (s = &fwds; *s; s = &(*s)->nextfwd)
            if (!strcmp((*s)->name, name)) {
                struct tokenval tokval;
                expr *e;
                char *p = nasm_skip_spaces(nasm_skip_word(special));

                stdscan_reset();
                stdscan_set(p);
                tokval.t_type = TOKEN_INVALID;
                e = evaluate(stdscan, NULL, &tokval, NULL, 1, nasm_error, NULL);
                if (e) {
                    if (!is_simple(e))
                        nasm_error(ERR_NONFATAL, "cannot use relocatable"
                              " expression as symbol size");
                    else
                        (*s)->size = reloc_value(e);
                }

                /*
                 * Remove it from the list of unresolved sizes.
                 */
                nasm_free((*s)->name);
                *s = (*s)->nextfwd;
                return;
            }
        return;                 /* it wasn't an important one */
    }

    saa_wbytes(strs, name, (int32_t)(1 + strlen(name)));
    strslen += 1 + strlen(name);

    lastsym = sym = saa_wstruct(syms);

    memset(&sym->symv, 0, sizeof(struct rbtree));

    sym->strpos = pos;
    sym->type = is_global ? SYM_GLOBAL : SYM_LOCAL;
    sym->other = STV_DEFAULT;
    sym->size = 0;
    if (segment == NO_SEG)
        sym->section = SHN_ABS;
    else {
        int i;
        sym->section = SHN_UNDEF;
        if (segment == def_seg) {
            /* we have to be sure at least text section is there */
            int tempint;
            if (segment != elf_section_names(".text", 2, &tempint))
                nasm_error(ERR_PANIC, "strange segment conditions in ELF driver");
        }
        for (i = 0; i < nsects; i++) {
            if (segment == sects[i]->index) {
                sym->section = i + 1;
                break;
            }
        }
    }

    if (is_global == 2) {
        sym->size = offset;
        sym->symv.key = 0;
        sym->section = SHN_COMMON;
        /*
         * We have a common variable. Check the special text to see
         * if it's a valid number and power of two; if so, store it
         * as the alignment for the common variable.
         */
        if (special) {
            bool err;
            sym->symv.key = readnum(special, &err);
            if (err)
                nasm_error(ERR_NONFATAL, "alignment constraint `%s' is not a"
                      " valid number", special);
            else if ((sym->symv.key | (sym->symv.key - 1)) != 2 * sym->symv.key - 1)
                nasm_error(ERR_NONFATAL, "alignment constraint `%s' is not a"
                      " power of two", special);
        }
        special_used = true;
    } else
        sym->symv.key = (sym->section == SHN_UNDEF ? 0 : offset);

    if (sym->type == SYM_GLOBAL) {
        /*
         * If sym->section == SHN_ABS, then the first line of the
         * else section would cause a core dump, because its a reference
         * beyond the end of the section array.
         * This behaviour is exhibited by this code:
         *     GLOBAL crash_nasm
         *     crash_nasm equ 0
         * To avoid such a crash, such requests are silently discarded.
         * This may not be the best solution.
         */
        if (sym->section == SHN_UNDEF || sym->section == SHN_COMMON) {
            bsym = raa_write(bsym, segment, nglobs);
        } else if (sym->section != SHN_ABS) {
            /*
             * This is a global symbol; so we must add it to the rbtree
             * of global symbols in its section.
             *
             * In addition, we check the special text for symbol
             * type and size information.
             */
            sects[sym->section-1]->gsyms =
                rb_insert(sects[sym->section-1]->gsyms, &sym->symv);

            if (special) {
                int n = strcspn(special, " \t");

                if (!nasm_strnicmp(special, "function", n))
                    sym->type |= STT_FUNC;
                else if (!nasm_strnicmp(special, "data", n) ||
                         !nasm_strnicmp(special, "object", n))
                    sym->type |= STT_OBJECT;
                else if (!nasm_strnicmp(special, "notype", n))
                    sym->type |= STT_NOTYPE;
                else
                    nasm_error(ERR_NONFATAL, "unrecognised symbol type `%.*s'",
                          n, special);
                special += n;

                special = nasm_skip_spaces(special);
                if (*special) {
                    n = strcspn(special, " \t");
                    if (!nasm_strnicmp(special, "default", n))
                        sym->other = STV_DEFAULT;
                    else if (!nasm_strnicmp(special, "internal", n))
                        sym->other = STV_INTERNAL;
                    else if (!nasm_strnicmp(special, "hidden", n))
                        sym->other = STV_HIDDEN;
                    else if (!nasm_strnicmp(special, "protected", n))
                        sym->other = STV_PROTECTED;
                    else
                        n = 0;
                    special += n;
                }

                if (*special) {
                    struct tokenval tokval;
                    expr *e;
                    int fwd = 0;
                    char *saveme = stdscan_get();

                    while (special[n] && nasm_isspace(special[n]))
                        n++;
                    /*
                     * We have a size expression; attempt to
                     * evaluate it.
                     */
                    stdscan_reset();
                    stdscan_set(special + n);
                    tokval.t_type = TOKEN_INVALID;
                    e = evaluate(stdscan, NULL, &tokval, &fwd, 0, nasm_error,
                                 NULL);
                    if (fwd) {
                        sym->nextfwd = fwds;
                        fwds = sym;
                        sym->name = nasm_strdup(name);
                    } else if (e) {
                        if (!is_simple(e))
                            nasm_error(ERR_NONFATAL, "cannot use relocatable"
                                  " expression as symbol size");
                        else
                            sym->size = reloc_value(e);
                    }
                    stdscan_set(saveme);
                }
                special_used = true;
            }
            /*
             * If TLS segment, mark symbol accordingly.
             */
            if (sects[sym->section - 1]->flags & SHF_TLS) {
                sym->type &= 0xf0;
                sym->type |= STT_TLS;
            }
        }
        sym->globnum = nglobs;
        nglobs++;
    } else
        nlocals++;

    if (special && !special_used)
        nasm_error(ERR_NONFATAL, "no special symbol features supported here");
}

static void elf_add_reloc(struct Section *sect, int32_t segment,
                          int64_t offset, int type)
{
    struct Reloc *r;

    r = *sect->tail = nasm_zalloc(sizeof(struct Reloc));
    sect->tail = &r->next;

    r->address = sect->len;
    r->offset = offset;

    if (segment != NO_SEG) {
        int i;
        for (i = 0; i < nsects; i++)
            if (segment == sects[i]->index)
                r->symbol = i + 2;
        if (!r->symbol)
            r->symbol = GLOBAL_TEMP_BASE + raa_read(bsym, segment);
    }
    r->type = type;

    sect->nrelocs++;
}

/*
 * This routine deals with ..got and ..sym relocations: the more
 * complicated kinds. In shared-library writing, some relocations
 * with respect to global symbols must refer to the precise symbol
 * rather than referring to an offset from the base of the section
 * _containing_ the symbol. Such relocations call to this routine,
 * which searches the symbol list for the symbol in question.
 *
 * R_386_GOT32 references require the _exact_ symbol address to be
 * used; R_386_32 references can be at an offset from the symbol.
 * The boolean argument `exact' tells us this.
 *
 * Return value is the adjusted value of `addr', having become an
 * offset from the symbol rather than the section. Should always be
 * zero when returning from an exact call.
 *
 * Limitation: if you define two symbols at the same place,
 * confusion will occur.
 *
 * Inefficiency: we search, currently, using a linked list which
 * isn't even necessarily sorted.
 */
static void elf_add_gsym_reloc(struct Section *sect,
                               int32_t segment, uint64_t offset, int64_t pcrel,
                               int type, bool exact)
{
    struct Reloc *r;
    struct Section *s;
    struct Symbol *sym;
    struct rbtree *srb;
    int i;

    /*
     * First look up the segment/offset pair and find a global
     * symbol corresponding to it. If it's not one of our segments,
     * then it must be an external symbol, in which case we're fine
     * doing a normal elf_add_reloc after first sanity-checking
     * that the offset from the symbol is zero.
     */
    s = NULL;
    for (i = 0; i < nsects; i++)
        if (segment == sects[i]->index) {
            s = sects[i];
            break;
        }

    if (!s) {
        if (exact && offset)
            nasm_error(ERR_NONFATAL, "invalid access to an external symbol");
        else
            elf_add_reloc(sect, segment, offset - pcrel, type);
        return;
    }

    srb = rb_search(s->gsyms, offset);
    if (!srb || (exact && srb->key != offset)) {
        nasm_error(ERR_NONFATAL, "unable to find a suitable global symbol"
              " for this reference");
        return;
    }
    sym = container_of(srb, struct Symbol, symv);

    r = *sect->tail = nasm_malloc(sizeof(struct Reloc));
    sect->tail = &r->next;
    r->next = NULL;

    r->address = sect->len;
    r->offset = offset - pcrel - sym->symv.key;
    r->symbol = GLOBAL_TEMP_BASE + sym->globnum;
    r->type = type;

    sect->nrelocs++;
}

static void elf_out(int32_t segto, const void *data,
                    enum out_type type, uint64_t size,
                    int32_t segment, int32_t wrt)
{
    struct Section *s;
    int64_t addr;
    int reltype, bytes;
    int i;
    static struct symlininfo sinfo;

#if defined(DEBUG) && DEBUG>2
    if (data)
        nasm_error(ERR_DEBUG,
                   " elf_out line: %d type: %x seg: %"PRIx32" segto: %"PRIx32" bytes: %"PRIx64" data: %"PRIx64"\n",
                   currentline, type, segment, segto, size, *(int64_t *)data);
    else
        nasm_error(ERR_DEBUG,
                   " elf_out line: %d type: %x seg: %"PRIx32" segto: %"PRIx32" bytes: %"PRIx64"\n",
                   currentline, type, segment, segto, size);
#endif

    /*
     * handle absolute-assembly (structure definitions)
     */
    if (segto == NO_SEG) {
        if (type != OUT_RESERVE)
            nasm_error(ERR_NONFATAL, "attempt to assemble code in [ABSOLUTE]"
                  " space");
        return;
    }

    s = NULL;
    for (i = 0; i < nsects; i++)
        if (segto == sects[i]->index) {
            s = sects[i];
            break;
        }
    if (!s) {
        int tempint;            /* ignored */
        if (segto != elf_section_names(".text", 2, &tempint))
            nasm_error(ERR_PANIC, "strange segment conditions in ELF driver");
        else {
            s = sects[nsects - 1];
            i = nsects - 1;
        }
    }

    /* again some stabs debugging stuff */
    if (of_elf64.current_dfmt) {
        sinfo.offset = s->len;
        sinfo.section = i;
        sinfo.segto = segto;
        sinfo.name = s->name;
        of_elf64.current_dfmt->debug_output(TY_DEBUGSYMLIN, &sinfo);
    }
    /* end of debugging stuff */

    if (s->type == SHT_NOBITS && type != OUT_RESERVE) {
        nasm_error(ERR_WARNING, "attempt to initialize memory in"
              " BSS section `%s': ignored", s->name);
        s->len += realsize(type, size);
        return;
    }

    switch (type) {
    case OUT_RESERVE:
        if (s->type == SHT_PROGBITS) {
            nasm_error(ERR_WARNING, "uninitialized space declared in"
                  " non-BSS section `%s': zeroing", s->name);
            elf_sect_write(s, NULL, size);
        } else
            s->len += size;
	break;

    case OUT_RAWDATA:
        if (segment != NO_SEG)
            nasm_error(ERR_PANIC, "OUT_RAWDATA with other than NO_SEG");
        elf_sect_write(s, data, size);
	break;

    case OUT_ADDRESS:
        addr = *(int64_t *)data;
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment % 2) {
            nasm_error(ERR_NONFATAL, "ELF format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                switch ((int)size) {
                case 1:
                    elf_add_reloc(s, segment, addr, R_X86_64_8);
                    break;
                case 2:
                    elf_add_reloc(s, segment, addr, R_X86_64_16);
                    break;
                case 4:
                    elf_add_reloc(s, segment, addr, R_X86_64_32);
                    break;
                case 8:
                    elf_add_reloc(s, segment, addr, R_X86_64_64);
                    break;
                default:
                    nasm_error(ERR_PANIC, "internal error elf64-hpa-871");
                    break;
                }
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1) {
                /*
                 * The user will supply GOT relative to $$. ELF
                 * will let us have GOT relative to $. So we
                 * need to fix up the data item by $-$$.
                 */
                addr += s->len;
                elf_add_reloc(s, segment, addr, R_X86_64_GOTPC32);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1) {
                if (size != 8) {
                    nasm_error(ERR_NONFATAL, "ELF64 requires ..gotoff "
                          "references to be qword");
                } else {
                    elf_add_reloc(s, segment, addr, R_X86_64_GOTOFF64);
                    addr = 0;
                }
            } else if (wrt == elf_got_sect + 1) {
                switch ((int)size) {
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_GOT32, true);
                    addr = 0;
                    break;
                case 8:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_GOT64, true);
                    addr = 0;
                    break;
                default:
                    nasm_error(ERR_NONFATAL, "invalid ..got reference");
                    break;
                }
            } else if (wrt == elf_sym_sect + 1) {
                switch ((int)size) {
                case 1:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_8, false);
                    addr = 0;
                    break;
                case 2:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_16, false);
                    addr = 0;
                    break;
                case 4:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_32, false);
                    addr = 0;
                    break;
                case 8:
                    elf_add_gsym_reloc(s, segment, addr, 0,
                                       R_X86_64_64, false);
                    addr = 0;
                    break;
                default:
                    nasm_error(ERR_PANIC, "internal error elf64-hpa-903");
                    break;
                }
            } else if (wrt == elf_plt_sect + 1) {
                nasm_error(ERR_NONFATAL, "ELF format cannot produce non-PC-"
                      "relative PLT references");
            } else {
                nasm_error(ERR_NONFATAL, "ELF format does not support this"
                      " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, size);
	break;

    case OUT_REL1ADR:
	reltype = R_X86_64_PC8;
	bytes = 1;
	goto rel12adr;

    case OUT_REL2ADR:
	reltype = R_X86_64_PC16;
	bytes = 2;
	goto rel12adr;

    rel12adr:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_error(ERR_PANIC, "intra-segment OUT_REL1ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment % 2) {
            nasm_error(ERR_NONFATAL, "ELF format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, reltype);
                addr = 0;
            } else {
                nasm_error(ERR_NONFATAL,
                      "Unsupported non-32-bit ELF relocation");
            }
        }
        elf_sect_writeaddr(s, addr, bytes);
	break;

    case OUT_REL4ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_error(ERR_PANIC, "intra-segment OUT_REL4ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment % 2) {
            nasm_error(ERR_NONFATAL, "ELF64 format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, R_X86_64_PC32);
                addr = 0;
            } else if (wrt == elf_plt_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_PLT32, true);
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTPCREL, true);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_error(ERR_NONFATAL, "ELF64 requires ..gotoff references to be "
                      "qword absolute");
            } else if (wrt == elf_gottpoff_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTTPOFF, true);
                addr = 0;
            } else {
                nasm_error(ERR_NONFATAL, "ELF64 format does not support this"
                      " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, 4);
	break;

    case OUT_REL8ADR:
        addr = *(int64_t *)data - size;
        if (segment == segto)
            nasm_error(ERR_PANIC, "intra-segment OUT_REL8ADR");
        if (segment == NO_SEG) {
            /* Do nothing */
        } else if (segment % 2) {
            nasm_error(ERR_NONFATAL, "ELF64 format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                elf_add_reloc(s, segment, addr, R_X86_64_PC64);
                addr = 0;
            } else if (wrt == elf_gotpc_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                elf_add_gsym_reloc(s, segment, addr+size, size,
                                   R_X86_64_GOTPCREL64, true);
                addr = 0;
            } else if (wrt == elf_gotoff_sect + 1 ||
                       wrt == elf_got_sect + 1) {
                nasm_error(ERR_NONFATAL, "ELF64 requires ..gotoff references to be "
                      "absolute");
            } else if (wrt == elf_gottpoff_sect + 1) {
                nasm_error(ERR_NONFATAL, "ELF64 requires ..gottpoff references to be "
                      "dword");
            } else {
                nasm_error(ERR_NONFATAL, "ELF64 format does not support this"
                      " use of WRT");
            }
        }
        elf_sect_writeaddr(s, addr, 8);
	break;
    }
}

static void elf_write(void)
{
    int align;
    char *p;
    int i;

    struct SAA *symtab;
    int32_t symtablen, symtablocal;

    /*
     * Work out how many sections we will have. We have SHN_UNDEF,
     * then the flexible user sections, then the fixed sections
     * `.shstrtab', `.symtab' and `.strtab', then optionally
     * relocation sections for the user sections.
     */
    nsections = sec_numspecial + 1;
    if (of_elf64.current_dfmt == &df_stabs)
        nsections += 3;
    else if (of_elf64.current_dfmt == &df_dwarf)
        nsections += 10;

    add_sectname("", ".shstrtab");
    add_sectname("", ".symtab");
    add_sectname("", ".strtab");
    for (i = 0; i < nsects; i++) {
        nsections++;            /* for the section itself */
        if (sects[i]->head) {
            nsections++;        /* for its relocations */
            add_sectname(".rela", sects[i]->name);
        }
    }

    if (of_elf64.current_dfmt == &df_stabs) {
        /* in case the debug information is wanted, just add these three sections... */
        add_sectname("", ".stab");
        add_sectname("", ".stabstr");
        add_sectname(".rel", ".stab");
    }

    else if (of_elf64.current_dfmt == &df_dwarf) {
        /* the dwarf debug standard specifies the following ten sections,
           not all of which are currently implemented,
           although all of them are defined. */
        #define debug_aranges (int64_t) (nsections-10)
        #define debug_info (int64_t) (nsections-7)
        #define debug_abbrev (int64_t) (nsections-5)
        #define debug_line (int64_t) (nsections-4)
        add_sectname("", ".debug_aranges");
        add_sectname(".rela", ".debug_aranges");
        add_sectname("", ".debug_pubnames");
        add_sectname("", ".debug_info");
        add_sectname(".rela", ".debug_info");
        add_sectname("", ".debug_abbrev");
        add_sectname("", ".debug_line");
        add_sectname(".rela", ".debug_line");
        add_sectname("", ".debug_frame");
        add_sectname("", ".debug_loc");
    }

    /*
     * Output the ELF header.
     */
    fwrite("\177ELF\2\1\1", 7, 1, ofile);
    fputc(elf_osabi, ofile);
    fputc(elf_abiver, ofile);
    fwritezero(7, ofile);
    fwriteint16_t(ET_REL, ofile);      /* relocatable file */
    fwriteint16_t(EM_X86_64, ofile);      /* processor ID */
    fwriteint32_t(1L, ofile);      /* EV_CURRENT file format version */
    fwriteint64_t(0L, ofile);      /* no entry point */
    fwriteint64_t(0L, ofile);      /* no program header table */
    fwriteint64_t(0x40L, ofile);   /* section headers straight after
                                 * ELF header plus alignment */
    fwriteint32_t(0L, ofile);      /* 386 defines no special flags */
    fwriteint16_t(0x40, ofile);   /* size of ELF header */
    fwriteint16_t(0, ofile);      /* no program header table, again */
    fwriteint16_t(0, ofile);      /* still no program header table */
    fwriteint16_t(sizeof(Elf64_Shdr), ofile);   /* size of section header */
    fwriteint16_t(nsections, ofile);      /* number of sections */
    fwriteint16_t(sec_shstrtab, ofile);   /* string table section index for
                                           * section header table */

    /*
     * Build the symbol table and relocation tables.
     */
    symtab = elf_build_symtab(&symtablen, &symtablocal);
    for (i = 0; i < nsects; i++)
        if (sects[i]->head)
            sects[i]->rel = elf_build_reltab(&sects[i]->rellen,
                                             sects[i]->head);

    /*
     * Now output the section header table.
     */

    elf_foffs = 0x40 + sizeof(Elf64_Shdr) * nsections;
    align = ALIGN(elf_foffs, SEC_FILEALIGN) - elf_foffs;
    elf_foffs += align;
    elf_nsect = 0;
    elf_sects = nasm_malloc(sizeof(*elf_sects) * nsections);

    /* SHN_UNDEF */
    elf_section_header(0, SHT_NULL, 0, NULL, false, 0, SHN_UNDEF, 0, 0, 0);
    p = shstrtab + 1;

    /* The normal sections */
    for (i = 0; i < nsects; i++) {
        elf_section_header(p - shstrtab, sects[i]->type, sects[i]->flags,
                           (sects[i]->type == SHT_PROGBITS ?
                            sects[i]->data : NULL), true,
                           sects[i]->len, 0, 0, sects[i]->align, 0);
        p += strlen(p) + 1;
    }

    /* .shstrtab */
    elf_section_header(p - shstrtab, SHT_STRTAB, 0, shstrtab, false,
                       shstrtablen, 0, 0, 1, 0);
    p += strlen(p) + 1;

    /* .symtab */
    elf_section_header(p - shstrtab, SHT_SYMTAB, 0, symtab, true,
                       symtablen, sec_strtab, symtablocal, 4, 24);
    p += strlen(p) + 1;

    /* .strtab */
    elf_section_header(p - shstrtab, SHT_STRTAB, 0, strs, true,
                       strslen, 0, 0, 1, 0);
    p += strlen(p) + 1;

    /* The relocation sections */
    for (i = 0; i < nsects; i++)
        if (sects[i]->head) {
            elf_section_header(p - shstrtab, SHT_RELA, 0, sects[i]->rel, true,
                               sects[i]->rellen, sec_symtab, i + 1, 4, 24);
            p += strlen(p) + 1;
        }

    if (of_elf64.current_dfmt == &df_stabs) {
        /* for debugging information, create the last three sections
           which are the .stab , .stabstr and .rel.stab sections respectively */

        /* this function call creates the stab sections in memory */
        stabs64_generate();

        if (stabbuf && stabstrbuf && stabrelbuf) {
            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, stabbuf, false,
                               stablen, sec_stabstr, 0, 4, 12);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_STRTAB, 0, stabstrbuf, false,
                               stabstrlen, 0, 0, 4, 0);
            p += strlen(p) + 1;

            /* link -> symtable  info -> section to refer to */
            elf_section_header(p - shstrtab, SHT_REL, 0, stabrelbuf, false,
                               stabrellen, symtabsection, sec_stab, 4, 16);
            p += strlen(p) + 1;
        }
    } else if (of_elf64.current_dfmt == &df_dwarf) {
            /* for dwarf debugging information, create the ten dwarf sections */

            /* this function call creates the dwarf sections in memory */
            if (dwarf_fsect)
                dwarf64_generate();

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, arangesbuf, false,
                               arangeslen, 0, 0, 1, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_RELA, 0, arangesrelbuf, false,
                               arangesrellen, symtabsection, debug_aranges, 1, 24);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, pubnamesbuf, false,
                               pubnameslen, 0, 0, 1, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, infobuf, false,
                               infolen, 0, 0, 1, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_RELA, 0, inforelbuf, false,
                               inforellen, symtabsection, debug_info, 1, 24);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, abbrevbuf, false,
                               abbrevlen, 0, 0, 1, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, linebuf, false,
                               linelen, 0, 0, 1, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_RELA, 0, linerelbuf, false,
                               linerellen, symtabsection, debug_line, 1, 24);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, framebuf, false,
                               framelen, 0, 0, 8, 0);
            p += strlen(p) + 1;

            elf_section_header(p - shstrtab, SHT_PROGBITS, 0, locbuf, false,
                               loclen, 0, 0, 1, 0);
            p += strlen(p) + 1;
    }
    fwritezero(align, ofile);

    /*
     * Now output the sections.
     */
    elf_write_sections();

    nasm_free(elf_sects);
    saa_free(symtab);
}

static struct SAA *elf_build_symtab(int32_t *len, int32_t *local)
{
    struct SAA *s = saa_init(1L);
    struct Symbol *sym;
    uint8_t entry[24], *p;
    int i;

    *len = *local = 0;

    /*
     * First, an all-zeros entry, required by the ELF spec.
     */
    saa_wbytes(s, NULL, 24L);   /* null symbol table entry */
    *len += 24;
    (*local)++;

    /*
     * Next, an entry for the file name.
     */
    p = entry;
    WRITELONG(p, 1);            /* we know it's 1st entry in strtab */
    WRITESHORT(p, STT_FILE);    /* type FILE */
    WRITESHORT(p, SHN_ABS);
    WRITEDLONG(p, (uint64_t) 0);  /* no value */
    WRITEDLONG(p, (uint64_t) 0);  /* no size either */
    saa_wbytes(s, entry, 24L);
    *len += 24;
    (*local)++;

    /*
     * Now some standard symbols defining the segments, for relocation
     * purposes.
     */
    for (i = 1; i <= nsects; i++) {
        p = entry;
        WRITELONG(p, 0);        /* no symbol name */
        WRITESHORT(p, STT_SECTION);       /* type, binding, and visibility */
        WRITESHORT(p, i);       /* section id */
        WRITEDLONG(p, (uint64_t) 0);        /* offset zero */
        WRITEDLONG(p, (uint64_t) 0);        /* size zero */
        saa_wbytes(s, entry, 24L);
        *len += 24;
        (*local)++;
    }


    /*
     * Now the other local symbols.
     */
    saa_rewind(syms);
    while ((sym = saa_rstruct(syms))) {
        if (sym->type & SYM_GLOBAL)
            continue;
        p = entry;
        WRITELONG(p, sym->strpos);      /* index into symbol string table */
        WRITECHAR(p, sym->type);        /* type and binding */
        WRITECHAR(p, sym->other);       /* visibility */
        WRITESHORT(p, sym->section);    /* index into section header table */
        WRITEDLONG(p, (int64_t)sym->symv.key); /* value of symbol */
        WRITEDLONG(p, (int64_t)sym->size);  /* size of symbol */
        saa_wbytes(s, entry, 24L);
        *len += 24;
        (*local)++;
    }
     /*
      * dwarf needs symbols for debug sections
      * which are relocation targets.
      */
     if (of_elf64.current_dfmt == &df_dwarf) {
        dwarf_infosym = *local;
        p = entry;
        WRITELONG(p, 0);        /* no symbol name */
        WRITESHORT(p, STT_SECTION);       /* type, binding, and visibility */
        WRITESHORT(p, debug_info);       /* section id */
        WRITEDLONG(p, (uint64_t) 0);        /* offset zero */
        WRITEDLONG(p, (uint64_t) 0);        /* size zero */
        saa_wbytes(s, entry, 24L);
        *len += 24;
        (*local)++;
        dwarf_abbrevsym = *local;
        p = entry;
        WRITELONG(p, 0);        /* no symbol name */
        WRITESHORT(p, STT_SECTION);       /* type, binding, and visibility */
        WRITESHORT(p, debug_abbrev);       /* section id */
        WRITEDLONG(p, (uint64_t) 0);        /* offset zero */
        WRITEDLONG(p, (uint64_t) 0);        /* size zero */
        saa_wbytes(s, entry, 24L);
        *len += 24;
        (*local)++;
        dwarf_linesym = *local;
        p = entry;
        WRITELONG(p, 0);        /* no symbol name */
        WRITESHORT(p, STT_SECTION);       /* type, binding, and visibility */
        WRITESHORT(p, debug_line);       /* section id */
        WRITEDLONG(p, (uint64_t) 0);        /* offset zero */
        WRITEDLONG(p, (uint64_t) 0);        /* size zero */
        saa_wbytes(s, entry, 24L);
        *len += 24;
        (*local)++;
     }

    /*
     * Now the global symbols.
     */
    saa_rewind(syms);
    while ((sym = saa_rstruct(syms))) {
        if (!(sym->type & SYM_GLOBAL))
            continue;
        p = entry;
        WRITELONG(p, sym->strpos);
        WRITECHAR(p, sym->type);        /* type and binding */
        WRITECHAR(p, sym->other);       /* visibility */
        WRITESHORT(p, sym->section);
        WRITEDLONG(p, (int64_t)sym->symv.key);
        WRITEDLONG(p, (int64_t)sym->size);
        saa_wbytes(s, entry, 24L);
        *len += 24;
    }

    return s;
}

static struct SAA *elf_build_reltab(uint64_t *len, struct Reloc *r)
{
    struct SAA *s;
    uint8_t *p, entry[24];
    int32_t global_offset;

    if (!r)
        return NULL;

    s = saa_init(1L);
    *len = 0;

    /*
     * How to onvert from a global placeholder to a real symbol index;
     * the +2 refers to the two special entries, the null entry and
     * the filename entry.
     */
    global_offset = -GLOBAL_TEMP_BASE + nsects + nlocals + ndebugs + 2;

    while (r) {
        int32_t sym = r->symbol;

        if (sym >= GLOBAL_TEMP_BASE)
            sym += global_offset;

        p = entry;
        WRITEDLONG(p, r->address);
        WRITELONG(p, r->type);
        WRITELONG(p, sym);
        WRITEDLONG(p, r->offset);
        saa_wbytes(s, entry, 24L);
        *len += 24;

        r = r->next;
    }

    return s;
}

static void elf_section_header(int name, int type, uint64_t flags,
                               void *data, bool is_saa, uint64_t datalen,
                               int link, int info, int align, int eltsize)
{
    elf_sects[elf_nsect].data = data;
    elf_sects[elf_nsect].len = datalen;
    elf_sects[elf_nsect].is_saa = is_saa;
    elf_nsect++;

    fwriteint32_t((int32_t)name, ofile);
    fwriteint32_t((int32_t)type, ofile);
    fwriteint64_t((int64_t)flags, ofile);
    fwriteint64_t(0L, ofile);      /* no address, ever, in object files */
    fwriteint64_t(type == 0 ? 0L : elf_foffs, ofile);
    fwriteint64_t(datalen, ofile);
    if (data)
        elf_foffs += ALIGN(datalen, SEC_FILEALIGN);
    fwriteint32_t((int32_t)link, ofile);
    fwriteint32_t((int32_t)info, ofile);
    fwriteint64_t((int64_t)align, ofile);
    fwriteint64_t((int64_t)eltsize, ofile);
}

static void elf_write_sections(void)
{
    int i;
    for (i = 0; i < elf_nsect; i++)
        if (elf_sects[i].data) {
            int32_t len = elf_sects[i].len;
            int32_t reallen = ALIGN(len, SEC_FILEALIGN);
            int32_t align = reallen - len;
            if (elf_sects[i].is_saa)
                saa_fpwrite(elf_sects[i].data, ofile);
            else
                fwrite(elf_sects[i].data, len, 1, ofile);
            fwritezero(align, ofile);
        }
}

static void elf_sect_write(struct Section *sect, const void *data, size_t len)
{
    saa_wbytes(sect->data, data, len);
    sect->len += len;
}

static void elf_sect_writeaddr(struct Section *sect, int64_t data, size_t len)
{
    saa_writeaddr(sect->data, data, len);
    sect->len += len;
}

static void elf_sectalign(int32_t seg, unsigned int value)
{
    struct Section *s = NULL;
    int i;

    for (i = 0; i < nsects; i++) {
        if (sects[i]->index == seg) {
            s = sects[i];
            break;
        }
    }
    if (!s || !is_power2(value))
        return;

    if (value > s->align)
        s->align = value;
}

static int32_t elf_segbase(int32_t segment)
{
    return segment;
}

static int elf_directive(enum directives directive, char *value, int pass)
{
    bool err;
    int64_t n;
    char *p;

    switch (directive) {
    case D_OSABI:
        if (pass == 2)
            return 1; /* ignore in pass 2 */

        n = readnum(value, &err);
        if (err) {
            nasm_error(ERR_NONFATAL, "`osabi' directive requires a parameter");
            return 1;
        }
        if (n < 0 || n > 255) {
            nasm_error(ERR_NONFATAL, "valid osabi numbers are 0 to 255");
            return 1;
        }
        elf_osabi  = n;
        elf_abiver = 0;

        if ((p = strchr(value,',')) == NULL)
            return 1;

        n = readnum(p+1, &err);
        if (err || n < 0 || n > 255) {
            nasm_error(ERR_NONFATAL, "invalid ABI version number (valid: 0 to 255)");
            return 1;
        }

        elf_abiver = n;
        return 1;

    default:
        return 0;
    }
}

static void elf_filename(char *inname, char *outname)
{
    strcpy(elf_module, inname);
    standard_extension(inname, outname, ".o");
}

extern macros_t elf_stdmac[];

static int elf_set_info(enum geninfo type, char **val)
{
    (void)type;
    (void)val;
    return 0;
}
static struct dfmt df_dwarf = {
    "ELF64 (x86-64) dwarf debug format for Linux/Unix",
    "dwarf",
    dwarf64_init,
    dwarf64_linenum,
    debug64_deflabel,
    debug64_directive,
    debug64_typevalue,
    dwarf64_output,
    dwarf64_cleanup
};
static struct dfmt df_stabs = {
    "ELF64 (x86-64) stabs debug format for Linux/Unix",
    "stabs",
    null_debug_init,
    stabs64_linenum,
    debug64_deflabel,
    debug64_directive,
    debug64_typevalue,
    stabs64_output,
    stabs64_cleanup
};

struct dfmt *elf64_debugs_arr[3] = { &df_dwarf, &df_stabs, NULL };

struct ofmt of_elf64 = {
    "ELF64 (x86_64) object files (e.g. Linux)",
    "elf64",
    0,
    elf64_debugs_arr,
    &df_stabs,
    elf_stdmac,
    elf_init,
    elf_set_info,
    elf_out,
    elf_deflabel,
    elf_section_names,
    elf_sectalign,
    elf_segbase,
    elf_directive,
    elf_filename,
    elf_cleanup
};

/* common debugging routines */
static void debug64_deflabel(char *name, int32_t segment, int64_t offset,
                             int is_global, char *special)
{
    (void)name;
    (void)segment;
    (void)offset;
    (void)is_global;
    (void)special;
}

static void debug64_directive(const char *directive, const char *params)
{
    (void)directive;
    (void)params;
}

static void debug64_typevalue(int32_t type)
{
    int32_t stype, ssize;
    switch (TYM_TYPE(type)) {
        case TY_LABEL:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        case TY_BYTE:
            ssize = 1;
            stype = STT_OBJECT;
            break;
        case TY_WORD:
            ssize = 2;
            stype = STT_OBJECT;
            break;
        case TY_DWORD:
            ssize = 4;
            stype = STT_OBJECT;
            break;
        case TY_FLOAT:
            ssize = 4;
            stype = STT_OBJECT;
            break;
        case TY_QWORD:
            ssize = 8;
            stype = STT_OBJECT;
            break;
        case TY_TBYTE:
            ssize = 10;
            stype = STT_OBJECT;
            break;
        case TY_OWORD:
            ssize = 16;
            stype = STT_OBJECT;
            break;
        case TY_YWORD:
            ssize = 32;
            stype = STT_OBJECT;
            break;
        case TY_COMMON:
            ssize = 0;
            stype = STT_COMMON;
            break;
        case TY_SEG:
            ssize = 0;
            stype = STT_SECTION;
            break;
        case TY_EXTERN:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        case TY_EQU:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
        default:
            ssize = 0;
            stype = STT_NOTYPE;
            break;
    }
    if (stype == STT_OBJECT && lastsym && !lastsym->type) {
        lastsym->size = ssize;
        lastsym->type = stype;
    }
}

/* stabs debugging routines */

static void stabs64_linenum(const char *filename, int32_t linenumber, int32_t segto)
{
    (void)segto;
    if (!stabs_filename) {
        stabs_filename = (char *)nasm_malloc(strlen(filename) + 1);
        strcpy(stabs_filename, filename);
    } else {
        if (strcmp(stabs_filename, filename)) {
            /* yep, a memory leak...this program is one-shot anyway, so who cares...
               in fact, this leak comes in quite handy to maintain a list of files
               encountered so far in the symbol lines... */

            /* why not nasm_free(stabs_filename); we're done with the old one */

            stabs_filename = (char *)nasm_malloc(strlen(filename) + 1);
            strcpy(stabs_filename, filename);
        }
    }
    debug_immcall = 1;
    currentline = linenumber;
}


static void stabs64_output(int type, void *param)
{
    struct symlininfo *s;
    struct linelist *el;
    if (type == TY_DEBUGSYMLIN) {
        if (debug_immcall) {
            s = (struct symlininfo *)param;
            if (!(sects[s->section]->flags & SHF_EXECINSTR))
                return; /* line info is only collected for executable sections */
            numlinestabs++;
            el = (struct linelist *)nasm_malloc(sizeof(struct linelist));
            el->info.offset = s->offset;
            el->info.section = s->section;
            el->info.name = s->name;
            el->line = currentline;
            el->filename = stabs_filename;
            el->next = 0;
            if (stabslines) {
                stabslines->last->next = el;
                stabslines->last = el;
            } else {
                stabslines = el;
                stabslines->last = el;
            }
        }
    }
    debug_immcall = 0;
}

/* for creating the .stab , .stabstr and .rel.stab sections in memory */

static void stabs64_generate(void)
{
    int i, numfiles, strsize, numstabs = 0, currfile, mainfileindex;
    uint8_t *sbuf, *ssbuf, *rbuf, *sptr, *rptr;
    char **allfiles;
    int *fileidx;

    struct linelist *ptr;

    ptr = stabslines;

    allfiles = (char **)nasm_zalloc(numlinestabs * sizeof(char *));
    numfiles = 0;
    while (ptr) {
        if (numfiles == 0) {
            allfiles[0] = ptr->filename;
            numfiles++;
        } else {
            for (i = 0; i < numfiles; i++) {
                if (!strcmp(allfiles[i], ptr->filename))
                    break;
            }
            if (i >= numfiles) {
                allfiles[i] = ptr->filename;
                numfiles++;
            }
        }
        ptr = ptr->next;
    }
    strsize = 1;
    fileidx = (int *)nasm_malloc(numfiles * sizeof(int));
    for (i = 0; i < numfiles; i++) {
        fileidx[i] = strsize;
        strsize += strlen(allfiles[i]) + 1;
    }
    mainfileindex = 0;
    for (i = 0; i < numfiles; i++) {
        if (!strcmp(allfiles[i], elf_module)) {
            mainfileindex = i;
            break;
        }
    }

    /*
     * worst case size of the stab buffer would be:
     * the sourcefiles changes each line, which would mean 1 SOL, 1 SYMLIN per line
     * plus one "ending" entry
     */
    sbuf = (uint8_t *)nasm_malloc((numlinestabs * 2 + 4) *
                                    sizeof(struct stabentry));
    ssbuf = (uint8_t *)nasm_malloc(strsize);
    rbuf = (uint8_t *)nasm_malloc(numlinestabs * 16 * (2 + 3));
    rptr = rbuf;

    for (i = 0; i < numfiles; i++)
        strcpy((char *)ssbuf + fileidx[i], allfiles[i]);
    ssbuf[0] = 0;

    stabstrlen = strsize;       /* set global variable for length of stab strings */

    sptr = sbuf;
    ptr = stabslines;
    numstabs = 0;

    if (ptr) {
        /*
         * this is the first stab, its strx points to the filename of the
         * the source-file, the n_desc field should be set to the number
         * of remaining stabs
         */
        WRITE_STAB(sptr, fileidx[0], 0, 0, 0, strlen(allfiles[0] + 12));

        /* this is the stab for the main source file */
        WRITE_STAB(sptr, fileidx[mainfileindex], N_SO, 0, 0, 0);

        /* relocation table entry */

        /*
         * Since the symbol table has two entries before
         * the section symbols, the index in the info.section
         * member must be adjusted by adding 2
         */

        WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
        WRITELONG(rptr, R_X86_64_32);
        WRITELONG(rptr, ptr->info.section + 2);

        numstabs++;
        currfile = mainfileindex;
    }

    while (ptr) {
        if (strcmp(allfiles[currfile], ptr->filename)) {
            /* oops file has changed... */
            for (i = 0; i < numfiles; i++)
                if (!strcmp(allfiles[i], ptr->filename))
                    break;
            currfile = i;
            WRITE_STAB(sptr, fileidx[currfile], N_SOL, 0, 0,
                       ptr->info.offset);
            numstabs++;

            /* relocation table entry */

            WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
            WRITELONG(rptr, R_X86_64_32);
            WRITELONG(rptr, ptr->info.section + 2);
        }

        WRITE_STAB(sptr, 0, N_SLINE, 0, ptr->line, ptr->info.offset);
        numstabs++;

        /* relocation table entry */

        WRITEDLONG(rptr, (int64_t)(sptr - sbuf) - 4);
        WRITELONG(rptr, R_X86_64_32);
        WRITELONG(rptr, ptr->info.section + 2);

        ptr = ptr->next;

    }

    /* this is an "ending" token */
    WRITE_STAB(sptr, 0, N_SO, 0, 0, 0);
    numstabs++;

    ((struct stabentry *)sbuf)->n_desc = numstabs;

    nasm_free(allfiles);
    nasm_free(fileidx);

    stablen = (sptr - sbuf);
    stabrellen = (rptr - rbuf);
    stabrelbuf = rbuf;
    stabbuf = sbuf;
    stabstrbuf = ssbuf;
}

static void stabs64_cleanup(void)
{
    struct linelist *ptr, *del;
    if (!stabslines)
        return;

    ptr = stabslines;
    while (ptr) {
        del = ptr;
        ptr = ptr->next;
        nasm_free(del);
    }

    nasm_free(stabbuf);
    nasm_free(stabrelbuf);
    nasm_free(stabstrbuf);
}

/* dwarf routines */

static void dwarf64_init(void)
{
    ndebugs = 3; /* 3 debug symbols */
}

static void dwarf64_linenum(const char *filename, int32_t linenumber,
                            int32_t segto)
{
    (void)segto;
    dwarf64_findfile(filename);
    debug_immcall = 1;
    currentline = linenumber;
}

/* called from elf_out with type == TY_DEBUGSYMLIN */
static void dwarf64_output(int type, void *param)
{
    int ln, aa, inx, maxln, soc;
    struct symlininfo *s;
    struct SAA *plinep;

    (void)type;

    s = (struct symlininfo *)param;

    /* line number info is only gathered for executable sections */
    if (!(sects[s->section]->flags & SHF_EXECINSTR))
        return;

    /* Check if section index has changed */
    if (!(dwarf_csect && (dwarf_csect->section) == (s->section)))
        dwarf64_findsect(s->section);

    /* do nothing unless line or file has changed */
    if (!debug_immcall)
        return;

    ln = currentline - dwarf_csect->line;
    aa = s->offset - dwarf_csect->offset;
    inx = dwarf_clist->line;
    plinep = dwarf_csect->psaa;
    /* check for file change */
    if (!(inx == dwarf_csect->file)) {
        saa_write8(plinep,DW_LNS_set_file);
        saa_write8(plinep,inx);
        dwarf_csect->file = inx;
    }
    /* check for line change */
    if (ln) {
        /* test if in range of special op code */
        maxln = line_base + line_range;
        soc = (ln - line_base) + (line_range * aa) + opcode_base;
        if (ln >= line_base && ln < maxln && soc < 256) {
            saa_write8(plinep,soc);
        } else {
            saa_write8(plinep,DW_LNS_advance_line);
            saa_wleb128s(plinep,ln);
            if (aa) {
                saa_write8(plinep,DW_LNS_advance_pc);
                saa_wleb128u(plinep,aa);
            }
        }
        dwarf_csect->line = currentline;
        dwarf_csect->offset = s->offset;
    }

    /* show change handled */
    debug_immcall = 0;
}


static void dwarf64_generate(void)
{
    uint8_t *pbuf;
    int indx;
    struct linelist *ftentry;
    struct SAA *paranges, *ppubnames, *pinfo, *pabbrev, *plines, *plinep;
    struct SAA *parangesrel, *plinesrel, *pinforel;
    struct sectlist *psect;
    size_t saalen, linepoff, totlen, highaddr;

    /* write epilogues for each line program range */
    /* and build aranges section */
    paranges = saa_init(1L);
    parangesrel = saa_init(1L);
    saa_write16(paranges,3);            /* dwarf version */
    saa_write64(parangesrel, paranges->datalen+4);
    saa_write64(parangesrel, (dwarf_infosym << 32) +  R_X86_64_32); /* reloc to info */
    saa_write64(parangesrel, 0);
    saa_write32(paranges,0);            /* offset into info */
    saa_write8(paranges,8);             /* pointer size */
    saa_write8(paranges,0);             /* not segmented */
    saa_write32(paranges,0);            /* padding */
    /* iterate though sectlist entries */
     psect = dwarf_fsect;
     totlen = 0;
     highaddr = 0;
     for (indx = 0; indx < dwarf_nsections; indx++)
     {
         plinep = psect->psaa;
         /* Line Number Program Epilogue */
         saa_write8(plinep,2);			/* std op 2 */
         saa_write8(plinep,(sects[psect->section]->len)-psect->offset);
         saa_write8(plinep,DW_LNS_extended_op);
         saa_write8(plinep,1);			/* operand length */
         saa_write8(plinep,DW_LNE_end_sequence);
         totlen += plinep->datalen;
         /* range table relocation entry */
         saa_write64(parangesrel, paranges->datalen + 4);
         saa_write64(parangesrel, ((uint64_t) (psect->section + 2) << 32) +  R_X86_64_64);
         saa_write64(parangesrel, (uint64_t) 0);
         /* range table entry */
         saa_write64(paranges,0x0000);		/* range start */
         saa_write64(paranges,sects[psect->section]->len);	/* range length */
         highaddr += sects[psect->section]->len;
         /* done with this entry */
         psect = psect->next;
     }
    saa_write64(paranges,0);		/* null address */
    saa_write64(paranges,0);		/* null length */
    saalen = paranges->datalen;
    arangeslen = saalen + 4;
    arangesbuf = pbuf = nasm_malloc(arangeslen);
    WRITELONG(pbuf,saalen);			/* initial length */
    saa_rnbytes(paranges, pbuf, saalen);
    saa_free(paranges);

    /* build rela.aranges section */
    arangesrellen = saalen = parangesrel->datalen;
    arangesrelbuf = pbuf = nasm_malloc(arangesrellen); 
    saa_rnbytes(parangesrel, pbuf, saalen);
    saa_free(parangesrel);

    /* build pubnames section */
    ppubnames = saa_init(1L);
    saa_write16(ppubnames,3);   /* dwarf version */
    saa_write32(ppubnames,0);   /* offset into info */
    saa_write32(ppubnames,0);   /* space used in info */
    saa_write32(ppubnames,0);   /* end of list */
    saalen = ppubnames->datalen;
    pubnameslen = saalen + 4;
    pubnamesbuf = pbuf = nasm_malloc(pubnameslen);
    WRITELONG(pbuf,saalen);     /* initial length */
    saa_rnbytes(ppubnames, pbuf, saalen);
    saa_free(ppubnames);

    /* build info section */
    pinfo = saa_init(1L);
    pinforel = saa_init(1L);
    saa_write16(pinfo,3);			/* dwarf version */
    saa_write64(pinforel, pinfo->datalen + 4);
    saa_write64(pinforel, (dwarf_abbrevsym << 32) +  R_X86_64_32); /* reloc to abbrev */
    saa_write64(pinforel, 0);
    saa_write32(pinfo,0);			/* offset into abbrev */
    saa_write8(pinfo,8);			/* pointer size */
    saa_write8(pinfo,1);			/* abbrviation number LEB128u */
    saa_write64(pinforel, pinfo->datalen + 4);
    saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
    saa_write64(pinforel, 0);
    saa_write64(pinfo,0);			/* DW_AT_low_pc */
    saa_write64(pinforel, pinfo->datalen + 4);
    saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
    saa_write64(pinforel, 0);
    saa_write64(pinfo,highaddr);		/* DW_AT_high_pc */
    saa_write64(pinforel, pinfo->datalen + 4);
    saa_write64(pinforel, (dwarf_linesym << 32) +  R_X86_64_32); /* reloc to line */
    saa_write64(pinforel, 0);
    saa_write32(pinfo,0);			/* DW_AT_stmt_list */
    saa_wbytes(pinfo, elf_module, strlen(elf_module)+1);
    saa_wbytes(pinfo, nasm_signature, strlen(nasm_signature)+1);
    saa_write16(pinfo,DW_LANG_Mips_Assembler);
    saa_write8(pinfo,2);			/* abbrviation number LEB128u */
    saa_write64(pinforel, pinfo->datalen + 4);
    saa_write64(pinforel, ((uint64_t)(dwarf_fsect->section + 2) << 32) +  R_X86_64_64);
    saa_write64(pinforel, 0);
    saa_write64(pinfo,0);			/* DW_AT_low_pc */
    saa_write64(pinfo,0);			/* DW_AT_frame_base */
    saa_write8(pinfo,0);			/* end of entries */
    saalen = pinfo->datalen;
    infolen = saalen + 4;
    infobuf = pbuf = nasm_malloc(infolen);
    WRITELONG(pbuf,saalen);     /* initial length */
    saa_rnbytes(pinfo, pbuf, saalen);
    saa_free(pinfo);

    /* build rela.info section */
    inforellen = saalen = pinforel->datalen;
    inforelbuf = pbuf = nasm_malloc(inforellen);
    saa_rnbytes(pinforel, pbuf, saalen);
    saa_free(pinforel);

    /* build abbrev section */
    pabbrev = saa_init(1L);
    saa_write8(pabbrev,1);      /* entry number LEB128u */
    saa_write8(pabbrev,DW_TAG_compile_unit);    /* tag LEB128u */
    saa_write8(pabbrev,1);      /* has children */
    /* the following attributes and forms are all LEB128u values */
    saa_write8(pabbrev,DW_AT_low_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_high_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_stmt_list);
    saa_write8(pabbrev,DW_FORM_data4);
    saa_write8(pabbrev,DW_AT_name);
    saa_write8(pabbrev,DW_FORM_string);
    saa_write8(pabbrev,DW_AT_producer);
    saa_write8(pabbrev,DW_FORM_string);
    saa_write8(pabbrev,DW_AT_language);
    saa_write8(pabbrev,DW_FORM_data2);
    saa_write16(pabbrev,0);     /* end of entry */
    /* LEB128u usage same as above */
    saa_write8(pabbrev,2);      /* entry number */
    saa_write8(pabbrev,DW_TAG_subprogram);
    saa_write8(pabbrev,0);      /* no children */
    saa_write8(pabbrev,DW_AT_low_pc);
    saa_write8(pabbrev,DW_FORM_addr);
    saa_write8(pabbrev,DW_AT_frame_base);
    saa_write8(pabbrev,DW_FORM_data4);
    saa_write16(pabbrev,0);     /* end of entry */
    abbrevlen = saalen = pabbrev->datalen;
    abbrevbuf = pbuf = nasm_malloc(saalen);
    saa_rnbytes(pabbrev, pbuf, saalen);
    saa_free(pabbrev);

    /* build line section */
    /* prolog */
    plines = saa_init(1L);
    saa_write8(plines,1);           /* Minimum Instruction Length */
    saa_write8(plines,1);           /* Initial value of 'is_stmt' */
    saa_write8(plines,line_base);   /* Line Base */
    saa_write8(plines,line_range);  /* Line Range */
    saa_write8(plines,opcode_base); /* Opcode Base */
    /* standard opcode lengths (# of LEB128u operands) */
    saa_write8(plines,0);           /* Std opcode 1 length */
    saa_write8(plines,1);           /* Std opcode 2 length */
    saa_write8(plines,1);           /* Std opcode 3 length */
    saa_write8(plines,1);           /* Std opcode 4 length */
    saa_write8(plines,1);           /* Std opcode 5 length */
    saa_write8(plines,0);           /* Std opcode 6 length */
    saa_write8(plines,0);           /* Std opcode 7 length */
    saa_write8(plines,0);           /* Std opcode 8 length */
    saa_write8(plines,1);           /* Std opcode 9 length */
    saa_write8(plines,0);           /* Std opcode 10 length */
    saa_write8(plines,0);           /* Std opcode 11 length */
    saa_write8(plines,1);           /* Std opcode 12 length */
    /* Directory Table */
    saa_write8(plines,0);           /* End of table */
    /* File Name Table */
    ftentry = dwarf_flist;
    for (indx = 0;indx<dwarf_numfiles;indx++)
    {
      saa_wbytes(plines, ftentry->filename, (int32_t)(strlen(ftentry->filename) + 1));
      saa_write8(plines,0);			/* directory  LEB128u */
      saa_write8(plines,0);			/* time LEB128u */
      saa_write8(plines,0);			/* size LEB128u */
      ftentry = ftentry->next;
    }
    saa_write8(plines,0);			/* End of table */
    linepoff = plines->datalen;
    linelen = linepoff + totlen + 10;
    linebuf = pbuf = nasm_malloc(linelen);
    WRITELONG(pbuf,linelen-4);		/* initial length */
    WRITESHORT(pbuf,3);			/* dwarf version */
    WRITELONG(pbuf,linepoff);		/* offset to line number program */
    /* write line header */
    saalen = linepoff;
    saa_rnbytes(plines, pbuf, saalen);   /* read a given no. of bytes */
    pbuf += linepoff;
    saa_free(plines);
    /* concatonate line program ranges */
    linepoff += 13;
    plinesrel = saa_init(1L);
    psect = dwarf_fsect;
    for (indx = 0; indx < dwarf_nsections; indx++) {
        saa_write64(plinesrel, linepoff);
        saa_write64(plinesrel, ((uint64_t) (psect->section + 2) << 32) +  R_X86_64_64);
        saa_write64(plinesrel, (uint64_t) 0);
        plinep = psect->psaa;
        saalen = plinep->datalen;
        saa_rnbytes(plinep, pbuf, saalen);
        pbuf += saalen;
        linepoff += saalen;
        saa_free(plinep);
        /* done with this entry */
        psect = psect->next;
    }


    /* build rela.lines section */
    linerellen =saalen = plinesrel->datalen;
    linerelbuf = pbuf = nasm_malloc(linerellen);
    saa_rnbytes(plinesrel, pbuf, saalen);
    saa_free(plinesrel);

    /* build frame section */
    framelen = 4;
    framebuf = pbuf = nasm_malloc(framelen);
    WRITELONG(pbuf,framelen-4); /* initial length */

    /* build loc section */
    loclen = 16;
    locbuf = pbuf = nasm_malloc(loclen);
    WRITEDLONG(pbuf,0);  /* null  beginning offset */
    WRITEDLONG(pbuf,0);  /* null  ending offset */
}

static void dwarf64_cleanup(void)
{
    nasm_free(arangesbuf);
    nasm_free(arangesrelbuf);
    nasm_free(pubnamesbuf);
    nasm_free(infobuf);
    nasm_free(inforelbuf);
    nasm_free(abbrevbuf);
    nasm_free(linebuf);
    nasm_free(linerelbuf);
    nasm_free(framebuf);
    nasm_free(locbuf);
}

static void dwarf64_findfile(const char * fname)
{
    int finx;
    struct linelist *match;

    /* return if fname is current file name */
    if (dwarf_clist && !(strcmp(fname, dwarf_clist->filename)))
        return;

    /* search for match */
    match = 0;
    if (dwarf_flist) {
        match = dwarf_flist;
        for (finx = 0; finx < dwarf_numfiles; finx++) {
            if (!(strcmp(fname, match->filename))) {
                dwarf_clist = match;
                return;
            }
        }
    }

    /* add file name to end of list */
    dwarf_clist = (struct linelist *)nasm_malloc(sizeof(struct linelist));
    dwarf_numfiles++;
    dwarf_clist->line = dwarf_numfiles;
    dwarf_clist->filename = nasm_malloc(strlen(fname) + 1);
    strcpy(dwarf_clist->filename,fname);
    dwarf_clist->next = 0;
    if (!dwarf_flist) {     /* if first entry */
        dwarf_flist = dwarf_elist = dwarf_clist;
        dwarf_clist->last = 0;
    } else {                /* chain to previous entry */
        dwarf_elist->next = dwarf_clist;
        dwarf_elist = dwarf_clist;
    }
}

static void dwarf64_findsect(const int index)
{
    int sinx;
    struct sectlist *match;
    struct SAA *plinep;

    /* return if index is current section index */
    if (dwarf_csect && (dwarf_csect->section == index))
        return;

    /* search for match */
    match = 0;
    if (dwarf_fsect) {
        match = dwarf_fsect;
        for (sinx = 0; sinx < dwarf_nsections; sinx++) {
            if (match->section == index) {
                dwarf_csect = match;
                return;
            }
            match = match->next;
        }
    }

    /* add entry to end of list */
    dwarf_csect = (struct sectlist *)nasm_malloc(sizeof(struct sectlist));
    dwarf_nsections++;
    dwarf_csect->psaa = plinep = saa_init(1L);
    dwarf_csect->line = 1;
    dwarf_csect->offset = 0;
    dwarf_csect->file = 1;
    dwarf_csect->section = index;
    dwarf_csect->next = 0;
    /* set relocatable address at start of line program */
    saa_write8(plinep,DW_LNS_extended_op);
    saa_write8(plinep,9);   /* operand length */
    saa_write8(plinep,DW_LNE_set_address);
    saa_write64(plinep,0);  /* Start Address */

    if (!dwarf_fsect) { /* if first entry */
        dwarf_fsect = dwarf_esect = dwarf_csect;
        dwarf_csect->last = 0;
    } else {            /* chain to previous entry */
        dwarf_esect->next = dwarf_csect;
        dwarf_esect = dwarf_csect;
    }
}

#endif /* OF_ELF */
