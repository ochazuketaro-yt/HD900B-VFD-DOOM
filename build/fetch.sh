#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Download (and SHA-256 verify) the Bootlin cross toolchain into $DL_DIR (default build/dl).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); . "$HERE/config.sh"
DL_DIR=${DL_DIR:-$HERE/dl}; mkdir -p "$DL_DIR"
[ -z "${TOOLCHAIN_DIR:-}" ] || { echo "using TOOLCHAIN_DIR=$TOOLCHAIN_DIR"; exit 0; }
file=${TOOLCHAIN_NAME}.tar.xz
if [ -f "$DL_DIR/$file" ] && echo "$TOOLCHAIN_SHA256  $DL_DIR/$file" | sha256sum -c --status; then echo "ok (cached)  $file"; exit 0; fi
echo "fetching $TOOLCHAIN_URL"
python3 -c 'import sys,urllib.request;urllib.request.urlretrieve(sys.argv[1],sys.argv[2])' "$TOOLCHAIN_URL" "$DL_DIR/$file"
echo "$TOOLCHAIN_SHA256  $DL_DIR/$file" | sha256sum -c --status || { echo "SHA-256 mismatch for $file" >&2; rm -f "$DL_DIR/$file"; exit 1; }
echo "ok  $file"
