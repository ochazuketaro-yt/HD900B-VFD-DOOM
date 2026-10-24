#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# List the input devices: look for your USB keyboard and note its eventN (then:  EVDEV=/dev/input/eventN ./doomvfd.sh).
# A keyboard usually has "kbd" in Handlers and a large KEY bitmap; a receiver may show two interfaces (use the one that reports key presses:
# run  ./evlog 10  and press keys).
grep -E "^(N:|H:)" /proc/bus/input/devices
