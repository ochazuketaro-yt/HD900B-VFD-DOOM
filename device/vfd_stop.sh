#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Stop a running doomvfd.sh (it then restores the display to "HD900B" by itself).
kill $(pidof fbdoom_vfd) 2>/dev/null
