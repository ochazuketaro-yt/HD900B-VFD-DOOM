#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Stop everything, restore the display to "HD900B" and delete the directory this script lives in (default /tmp/vfd).
# Nothing else was ever modified.  (A DOOM.WAD that was only linked with --wad-on-device is not touched.)
DIR=$(cd "$(dirname "$0")" && pwd)
case "$DIR" in /tmp/*) ;; *) echo "refusing to delete $DIR (not under /tmp)"; exit 1;; esac
kill $(pidof fbdoom_vfd) 2>/dev/null; sleep 1
kill $(pidof vfdmux) 2>/dev/null
[ -x "$DIR/vfdctl" ] && "$DIR/vfdctl" pt HD900B
cd / && rm -rf "$DIR"
echo "removed $DIR"
