/* SPDX-License-Identifier: GPL-3.0-or-later */
/* vfdctl -- HiMedia HD900B front-panel VFD (PT6302-family controller, 9 digits of 5x7 dots) from an external process.
 *
 * Protocol (same as the stock DvdPlayer's HMVFD_PT6302VFD, see docs/VFD-NOTES.md):
 *   one exchange   = write(/dev/venus_vfdo, &a, 4); write(/dev/venus_vfd_wrctl, &b, 4); then poll read(wrctl) until (word & 2)
 *   ioctl(wrctl, 0x20007201) = disable the keypad auto-read,  0x20007202 = enable it again (always done around updates)
 *   digit i  : a = (code << 8) | (0x10 | i), b = 0x1130         code = ASCII - 0x10  (0x10 = blank)
 *   icon i   : a = (byte << 8) | (0x30 | i), b = 0x1130         (16 bytes: top-row icons / colons)
 *   CG char n: a = 0x20 | n, b = 0x110, then four words b = 0x10 and a fifth with b = 0x1010   (5 column bytes, bit0 = top row)
 *              CG 0 shares its RAM with the lower symbol bar / icons.
 *   then a = 0x19 (b = 0x1130) and a = 0x70 (b = 0x1110) to latch / display on.
 *
 * usage:  vfdctl pt STR            show up to 9 characters (this is also how the stock "HD900B" is restored)
 *         vfdctl ptclear           blank text and icons
 *         vfdctl ptscroll STR [ms] scroll STR through the 9 digits
 *         vfdctl ptsym s0..s15     16 icon bytes (hex), text blank
 *         vfdctl ptraw TXT32 [SYM32 [PORT10]]   raw RAM bytes in hex (text codes, icon bytes, CG0 / symbol bar bytes)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int fo = -1, fw = -1;

static int wr_at_once(unsigned a, unsigned b) {
    unsigned st = 0; int n;
    if (write(fo, &a, 4) != 4) return -1;
    if (write(fw, &b, 4) != 4) return -1;
    for (n = 0; n < 200; n++) {              /* 200 * ~10 ms safety timeout (HZ=100: usleep(1000) takes ~10 ms) */
        if (read(fw, &st, 4) == 4 && (st & 2)) return 0;
        usleep(1000);
    }
    return -2;
}
static int openv(void) {
    fo = open("/dev/venus_vfdo", O_RDWR);
    fw = open("/dev/venus_vfd_wrctl", O_RDWR);
    if (fo < 0 || fw < 0) { perror("open vfd"); return -1; }
    return 0;
}
static unsigned char pt_txt[16], pt_sym[16], pt_port[5];
static int pt_flush(void) {
    int i, rc = 0;
    ioctl(fw, 0x20007201);
    for (i = 0; i < 16 && !rc; i++) rc = wr_at_once(((unsigned)pt_sym[i] << 8) | (0x30 | i), 0x1130);
    for (i = 0; i < 16 && !rc; i++) rc = wr_at_once(((unsigned)pt_txt[i] << 8) | (0x10 | i), 0x1130);
    if (!rc) rc = wr_at_once(0x20, 0x110);
    for (i = 0; i < 4 && !rc; i++) rc = wr_at_once(pt_port[i], 0x10);
    if (!rc) rc = wr_at_once(pt_port[4], 0x1010);
    if (!rc) rc = wr_at_once(0x19, 0x1130);
    if (!rc) rc = wr_at_once(0x70, 0x1110);
    ioctl(fw, 0x20007202);
    return rc;
}
static void hexin(unsigned char *d, const char *h, int n) {
    char t[3] = {0}; int j;
    for (j = 0; j < n && h[2 * j] && h[2 * j + 1]; j++) { t[0] = h[2 * j]; t[1] = h[2 * j + 1]; d[j] = strtoul(t, 0, 16); }
}
static void pt_text(const char *s) {
    int i; memset(pt_txt, 0x10, 16);
    for (i = 0; i < 9 && s[i]; i++) pt_txt[i] = (unsigned char)(s[i] - 0x10);
}
static void usage(void) {
    fprintf(stderr, "usage: vfdctl pt STR | ptclear | ptscroll STR [ms] | ptsym s0..s15 | ptraw TXT32 [SYM32 [PORT10]]\n");
    exit(2);
}
int main(int argc, char **argv) {
    int i, rc = 0;
    if (argc < 2) usage();
    if (openv()) return 1;
    if (!strcmp(argv[1], "pt") && argc >= 3) { pt_text(argv[2]); rc = pt_flush(); }
    else if (!strcmp(argv[1], "ptclear")) { pt_text(""); rc = pt_flush(); }
    else if (!strcmp(argv[1], "ptsym") && argc >= 18) { pt_text(""); for (i = 0; i < 16; i++) pt_sym[i] = strtoul(argv[2 + i], 0, 16); rc = pt_flush(); }
    else if (!strcmp(argv[1], "ptraw") && argc >= 3) {
        memset(pt_txt, 0x10, 16); hexin(pt_txt, argv[2], 16);
        if (argc > 3) hexin(pt_sym, argv[3], 16);
        if (argc > 4) hexin(pt_port, argv[4], 5);
        rc = pt_flush();
    }
    else if (!strcmp(argv[1], "ptscroll") && argc >= 3) {
        char buf[256]; int ms = argc > 3 ? atoi(argv[3]) : 300, n, k;
        snprintf(buf, sizeof buf, "         %s         ", argv[2]); n = strlen(buf);
        for (k = 0; k + 9 <= n && !rc; k++) { char w[10]; memcpy(w, buf + k, 9); w[9] = 0; pt_text(w); rc = pt_flush(); usleep(ms * 1000); }
    }
    else usage();
    if (rc) fprintf(stderr, "vfd write failed rc=%d\n", rc);
    return rc ? 1 : 0;
}
