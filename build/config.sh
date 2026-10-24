# SPDX-License-Identifier: GPL-3.0-or-later
TOOLCHAIN_NAME="mips32el--uclibc--stable-2024.05-1"
TOOLCHAIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/mips32el/tarballs/${TOOLCHAIN_NAME}.tar.xz"
TOOLCHAIN_SHA256="ae4400a934074a3b1b39fb7ec79fb535da4e65b668f2330ca14576d072f6b13e"
HD_CFLAGS="-march=mips32r2 -mtune=24kc -mfp32 -mhard-float -fno-pie -O2"
