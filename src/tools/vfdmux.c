/* SPDX-License-Identifier: GPL-3.0-or-later */
/* vfdmux -- time-multiplexed 45x7 1-bit bitmap on the HD900B PT6302 VFD (9 digits x 5x7).
 * Only CG codes 1..4 are free (CG0 shares the symbol RAM), so the 9 digits are shown in 3 groups
 * (digits 0-3, 4-7, 8) one after another; each subframe redefines CG1..4 and re-points the digits.
 * frame[x] (x=0..44): bit0 = top row ... bit6 = bottom row.
 * usage: vfdmux PATTERN SECONDS [groups 1|2|3|nomux]
 *   patterns: diag rows box wave sweep checker fill file   (SECONDS <= 0: run until SIGINT/SIGTERM)
 *   file: $VFD_FRAME (default /tmp/vfd/frame), 140 bytes: [0..44] dim plane (lit on alternate passes, ~50% grey), [45..89] bright plane,
 *         [90..134] blink plane (toggled every VFD_BLINK x 10 ms via times()), [135..139] symbol RAM bytes = lower bar (CG0).
 *         bit0 = top row.                                                                        */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/times.h>
#include <sys/ioctl.h>

static int fo = -1, fw = -1;
static volatile int stop;
static int wr(unsigned a, unsigned b) {
    unsigned st = 0; long n;
    if (write(fo, &a, 4) != 4 || write(fw, &b, 4) != 4) return -1;
    for (n = 0; n < 2000000; n++) if (read(fw, &st, 4) == 4 && (st & 2)) return 0;
    return -2;
}
static void onsig(int s) { (void)s; stop = 1; }

static unsigned char frame[45];
static unsigned char symwant[5], symcur[5];   /* lower bar / icon symbol RAM = CG char 0 (cmd 0x20), frame file bytes 135..139 */
static int symvalid;
static unsigned char shown[9];            /* code currently shown on each digit (0x10 = blank) */
static unsigned char cgcur[5][5];

static int subframe(int g, int nomux_dummy) {
    int d0 = g * 4, nd = g == 2 ? 1 : 4, j, d, i, rc = 0;
    (void)nomux_dummy;
    for (d = 0; d < 9 && !rc; d++)                     /* digits that must go blank (not in this group) */
        if (shown[d] != 0x10 && !(d >= d0 && d < d0 + nd)) { rc = wr(((unsigned)0x10 << 8) | (0x10 | d), 0x1130); shown[d] = 0x10; }
    for (j = 0; j < nd && !rc; j++) {
        const unsigned char *src = frame + 5 * (d0 + j);
        if (memcmp(cgcur[1 + j], src, 5)) {
            rc = wr(0x20 | (1 + j), 0x110);
            for (i = 0; i < 4 && !rc; i++) rc = wr(src[i], 0x10);
            if (!rc) rc = wr(src[4], 0x1010);
            memcpy(cgcur[1 + j], src, 5);
        }
    }
    for (j = 0; j < nd && !rc; j++)
        if (shown[d0 + j] != 1 + j) { rc = wr(((unsigned)(1 + j) << 8) | (0x10 | (d0 + j)), 0x1130); shown[d0 + j] = 1 + j; }
    return rc;
}
static int ffd = -1;
static void gen(const char *p, int t) {
    int x, y; memset(frame, 0, 45);
    if (!strcmp(p, "file")) {      /* /tmp/vfd/frame: 45 bytes dim plane + 45 bytes bright plane; dim pixels lit on alternate passes (~50% grey) */
        unsigned char b[140]; memset(b, 0, 140); long nrd = 0;
        static int bp = 0, bn = 0, bon = 0; static long bt0;
        if (ffd < 0) ffd = open(getenv("VFD_FRAME") ? getenv("VFD_FRAME") : "/tmp/vfd/frame", O_RDONLY);
        if (ffd >= 0) nrd = pread(ffd, b, 140, 0);
        symvalid = (nrd >= 140); if (symvalid) memcpy(symwant, b + 135, 5);
        if (!bp) { const char *e = getenv("VFD_BLINK"); bp = e ? atoi(e) : 1; if (bp < 1) bp = 1; }
        if ((bn++ & 31) == 0) { struct tms tm; bon = (int)(((long)times(&tm) / bp) & 1); }   /* blink plane: toggles every bp ticks (10 ms) of times() */
        (void)bt0;
        for (x = 0; x < 45; x++) frame[x] = b[45 + x] | ((t & 1) ? b[x] : 0) | (bon ? b[90 + x] : 0);
        return;
    }
    for (x = 0; x < 45; x++) for (y = 0; y < 7; y++) {
        int on = 0;
        if (!strcmp(p, "diag")) on = (x % 7) == y;
        else if (!strcmp(p, "rows")) on = (y == (x / 5) % 7);             /* each digit: one row lit, rows 0..6 */
        else if (!strcmp(p, "box")) on = (y == 0 || y == 6 || x == 0 || x == 44);
        else if (!strcmp(p, "wave")) { static const int s[16] = {3,4,5,6,6,6,5,4,3,2,1,0,0,0,1,2}; on = (y == s[((x + t) & 15)]); }
        else if (!strcmp(p, "sweep")) on = (x == t % 45) || (x == (t + 1) % 45);
        else if (!strcmp(p, "checker")) on = ((x + y + (t >> 3)) & 1);
        else if (!strcmp(p, "fill")) on = 1;
        if (on) frame[x] |= 1 << y;
    }
}
int main(int argc, char **argv) {
    int secs, nomux, groups, rc = 0, t = 0, g; long sub = 0; time_t t0;
    if (argc < 3) { fprintf(stderr, "usage: vfdmux PATTERN SECONDS [groups 1|2|3|nomux]   (pattern file = live frame from /tmp/vfd/frame)\n"); return 2; }
    secs = atoi(argv[2]); nomux = 0; groups = 3;
    if (argc > 3) { if (!strcmp(argv[3], "nomux")) groups = 1; else groups = atoi(argv[3]); if (groups < 1 || groups > 3) groups = 3; }
    fo = open("/dev/venus_vfdo", O_RDWR); fw = open("/dev/venus_vfd_wrctl", O_RDWR);
    if (fo < 0 || fw < 0) { perror("open"); return 1; }
    signal(SIGINT, onsig); signal(SIGTERM, onsig);
    memset(shown, 0x10, 9); memset(cgcur, 0xff, sizeof cgcur);
    ioctl(fw, 0x20007201);
    /* blank everything incl. symbol RAM / bar first */
    for (g = 0; g < 16 && !rc; g++) rc = wr(((unsigned)0 << 8) | (0x30 | g), 0x1130);
    for (g = 0; g < 16 && !rc; g++) rc = wr(((unsigned)0x10 << 8) | (0x10 | g), 0x1130);
    if (!rc) rc = wr(0x20, 0x110);
    for (g = 0; g < 4 && !rc; g++) rc = wr(0, 0x10);
    if (!rc) rc = wr(0, 0x1010);
    if (!rc) rc = wr(0x19, 0x1130);
    if (!rc) rc = wr(0x70, 0x1110);
    t0 = time(0);
    while (!rc && !stop && (secs <= 0 || t < secs * 300)) {
        gen(argv[1], !strcmp(argv[1], "file") ? t++ : t++ / 6);
        if (symvalid && memcmp(symwant, symcur, 5)) {          /* update the lower bar only when it changed */
            int i;
            rc = wr(0x20, 0x110);
            for (i = 0; i < 4 && !rc; i++) rc = wr(symwant[i], 0x10);
            if (!rc) rc = wr(symwant[4], 0x1010);
            memcpy(symcur, symwant, 5);
        }
        for (g = 0; g < groups && !rc; g++) { rc = subframe(g, 0); sub++; }
    }
    ioctl(fw, 0x20007202);
    printf("pattern=%s groups=%d subframes=%ld frames_gen=%d rc=%d\n", argv[1], groups, sub, t, rc);
    return rc ? 1 : 0;
}
