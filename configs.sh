
export ZLIB_VERSION="1.2.8"
export LIBPNG_VERSION="1.5.17"
export NASM_VERSION="2.10.09"
export LIBJPEG_TURBO_VERSION="1.3.0"
export GRAPHICS_MAGICK_VERSION="1.3.23"
export REMOTE_PATH="graphicsmagick-${GRAPHICS_MAGICK_VERSION}.tgz"
export LIBPNG_REMOTE_PATH="libpng-${LIBPNG_VERSION}.tgz"
export LIBJPEG_TURBO_REMOTE_PATH="libjpeg_turbo-${LIBJPEG_TURBO_VERSION}.tgz"
export NASM_REMOTE_PATH="nasm-${NASM_VERSION}.tgz"
export ZLIB_REMOTE_PATH="zlib-${ZLIB_VERSION}.tgz"
# heroku dropped support for cedar, but leaving for if/when they introduce a
# new stack
export STACK="cedar-14"
export S3_BUCKET="heroku-buildpack-graphicsmagick"
