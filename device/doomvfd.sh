#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# doomvfd.sh [MODE] [SECONDS]   -- run fbDOOM on the HD900B front VFD (45x7 dots; this renderer uses 20x7).  Runs in the foreground.
#   MODE    menu (default)  logo -> menu (ORIG / WAVE / FREE / EXIT) -> play with the USB keyboard
#           demo | demo2 | demo3   DOOM's attract demos (no input)
#           auto            an autopilot walks, aims and shoots (god mode, zombies spawn ahead; no input)
#   SECONDS 0 (default) = until DOOM exits (menu EXIT / Backspace / vfd_stop.sh)
# Environment: WAD=path to your own DOOM.WAD (default: <this dir>/DOOM.WAD)   EVDEV=/dev/input/eventN (default event1, see find_keyboard.sh)
#              BLINK=N enemy blink half-period in 10 ms ticks (default 1)
DIR=$(cd "$(dirname "$0")" && pwd); cd "$DIR" || exit 1
MODE=${1:-menu}; T=${2:-0}
WAD=${WAD:-$DIR/DOOM.WAD}; EVDEV=${EVDEV:-/dev/input/event1}; BLINK=${BLINK:-1}
[ -f "$WAD" ] || { echo "WAD not found: $WAD   (copy your own DOOM.WAD there or set WAD=)"; exit 1; }
[ -c /dev/venus_vfdo ] || { echo "/dev/venus_vfdo not found: this is not an HD900B-class player"; exit 1; }
rm -f frame
export FBDOOM_RAY=1 FBDOOM_STYLE=G FBDOOM_COLS=20 FBDOOM_NOFB=1 FBDOOM_VFD="$DIR/frame"
case $MODE in
 menu)  A="-warp 1 1 -skill 2"; export FBDOOM_EVDEV="$EVDEV" FBDOOM_UI=1 FBDOOM_HUD=1 FBDOOM_ASSIST=1;;
 demo)  A="-playdemo demo1";;
 demo2) A="-playdemo demo2";;
 demo3) A="-playdemo demo3";;
 auto)  A="-warp 1 1 -skill 2"; export FBDOOM_AUTO=1 FBDOOM_GOD=1;;
 *) echo "unknown mode: $MODE"; exit 2;;
esac
cleanup() { kill $VP $DP 2>/dev/null; sleep 1; ./vfdctl pt HD900B; }
trap 'cleanup; exit 0' INT TERM
./fbdoom_vfd -iwad "$WAD" -nosound $A </dev/null >doom.log 2>&1 &
DP=$!
n=0; while [ ! -s frame ] && [ $n -lt 60 ]; do sleep 1; n=$((n+1)); done      # wait until DOOM has produced its first frame
[ -s frame ] || { echo "DOOM did not start, see $DIR/doom.log"; cleanup; exit 1; }
VFD_FRAME="$DIR/frame" VFD_BLINK=$BLINK ./vfdmux file 0 1 &
VP=$!
n=0; while kill -0 $DP 2>/dev/null && { [ "$T" -eq 0 ] || [ $n -lt "$T" ]; }; do sleep 1; n=$((n+1)); done
cleanup
