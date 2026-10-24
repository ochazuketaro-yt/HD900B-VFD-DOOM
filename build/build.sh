#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build dist/ (static MIPS32r2 o32 executables for the HD900B): fbdoom_vfd, vfdmux, vfdctl, evlog, tt.
#   build/build.sh                                   fetch the Bootlin toolchain (uClibc-ng, GCC 13.3), build
#   TOOLCHAIN_DIR=/opt/mips32el--uclibc--stable-2024.05-1 build/build.sh      reuse an extracted toolchain
# Host needs: bash, make, python3, xz, tar, sha256sum.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); ROOT=$(dirname "$HERE"); . "$HERE/config.sh"
DL_DIR=${DL_DIR:-$HERE/dl}; WORK=${WORK:-$HERE/work}; export DL_DIR; mkdir -p "$WORK" "$ROOT/dist"
export TOOLCHAIN_DIR=${TOOLCHAIN_DIR:-}
"$HERE/fetch.sh"
if [ -z "$TOOLCHAIN_DIR" ]; then TOOLCHAIN_DIR=$WORK/$TOOLCHAIN_NAME; [ -d "$TOOLCHAIN_DIR" ] || tar -C "$WORK" -xf "$DL_DIR/${TOOLCHAIN_NAME}.tar.xz"; fi
export PATH=$TOOLCHAIN_DIR/bin:$PATH
CC=mipsel-linux-gcc; STRIP=mipsel-linux-strip
command -v "$CC" >/dev/null || { echo "cross compiler not found in $TOOLCHAIN_DIR/bin" >&2; exit 1; }
"$CC" --version | head -1

echo "== fbdoom_vfd (fbDOOM + VFD renderer)"
rm -rf "$WORK/fbdoom" && mkdir -p "$WORK/fbdoom" && cp "$ROOT"/src/fbdoom/* "$WORK/fbdoom/"
make -C "$WORK/fbdoom" -s CC="$CC" CROSS_COMPILE=mipsel-linux- NOSDL=1 \
  CFLAGS="$HD_CFLAGS -Os -DNORMALUNIX -DLINUX -DSNDSERV" LDFLAGS="-static -no-pie -Wl,--gc-sections" 2>&1 | grep -E "error|Error" && exit 1
$STRIP -o "$ROOT/dist/fbdoom_vfd" "$WORK/fbdoom/fbdoom"

echo "== small tools"
for t in vfdmux vfdctl evlog tt; do
  $CC $HD_CFLAGS -Os -static -Wall -o "$WORK/$t.unstripped" "$ROOT/src/tools/$t.c"
  $STRIP -o "$ROOT/dist/$t" "$WORK/$t.unstripped"
done
( cd "$ROOT/dist" && sha256sum fbdoom_vfd vfdmux vfdctl evlog tt > SHA256SUMS && cat SHA256SUMS )
