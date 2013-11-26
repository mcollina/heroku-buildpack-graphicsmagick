/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2012 The NASM Authors - All Rights Reserved
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
 * This is a null preprocessor which just copies lines from input
 * to output. It's used when someone explicitly requests that NASM
 * not preprocess their source file.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>

#include "nasm.h"
#include "nasmlib.h"
#include "preproc.h"

#define BUF_DELTA 512

static FILE *nop_fp;
static ListGen *nop_list;
static int32_t nop_lineinc;

static void nop_reset(char *file, int pass, ListGen *listgen, StrList **deplist)
{
    src_set_fname(nasm_strdup(file));
    src_set_linnum(0);
    nop_lineinc = 1;
    nop_fp = fopen(file, "r");

    if (!nop_fp)
        nasm_error(ERR_FATAL | ERR_NOFILE,
		   "unable to open input file `%s'", file);
    nop_list = listgen;
    (void)pass;                 /* placate compilers */

    if (deplist) {
	StrList *sl = nasm_malloc(strlen(file)+1+sizeof sl->next);
	sl->next = NULL;
	strcpy(sl->str, file);
	*deplist = sl;
    }
}

static char *nop_getline(void)
{
    char *buffer, *p, *q;
    int bufsize;

    bufsize = BUF_DELTA;
    buffer = nasm_malloc(BUF_DELTA);
    src_set_linnum(src_get_linnum() + nop_lineinc);

    while (1) {                 /* Loop to handle %line */

        p = buffer;
        while (1) {             /* Loop to handle long lines */
            q = fgets(p, bufsize - (p - buffer), nop_fp);
            if (!q)
                break;
            p += strlen(p);
            if (p > buffer && p[-1] == '\n')
                break;
            if (p - buffer > bufsize - 10) {
                int offset;
                offset = p - buffer;
                bufsize += BUF_DELTA;
                buffer = nasm_realloc(buffer, bufsize);
                p = buffer + offset;
            }
        }

        if (!q && p == buffer) {
            nasm_free(buffer);
            return NULL;
        }

        /*
         * Play safe: remove CRs, LFs and any spurious ^Zs, if any of
         * them are present at the end of the line.
         */
        buffer[strcspn(buffer, "\r\n\032")] = '\0';

        if (!nasm_strnicmp(buffer, "%line", 5)) {
            int32_t ln;
            int li;
            char *nm = nasm_malloc(strlen(buffer));
            if (sscanf(buffer + 5, "%"PRId32"+%d %s", &ln, &li, nm) == 3) {
                nasm_free(src_set_fname(nm));
                src_set_linnum(ln);
                nop_lineinc = li;
                continue;
            }
            nasm_free(nm);
        }
        break;
    }

    nop_list->line(LIST_READ, buffer);

    return buffer;
}

static void nop_cleanup(int pass)
{
    (void)pass;                     /* placate GCC */
    if (nop_fp) {
        fclose(nop_fp);
        nop_fp = NULL;
    }
}

static void nop_extra_stdmac(macros_t *macros)
{
    (void)macros;
}

static void nop_pre_define(char *definition)
{
    (void)definition;
}

static void nop_pre_undefine(char *definition)
{
    (void)definition;
}

static void nop_pre_include(char *fname)
{
    (void)fname;
}

static void nop_include_path(char *path)
{
    (void)path;
}

struct preproc_ops preproc_nop = {
    nop_reset,
    nop_getline,
    nop_cleanup,
    nop_extra_stdmac,
    nop_pre_define,
    nop_pre_undefine,
    nop_pre_include,
    nop_include_path
};
