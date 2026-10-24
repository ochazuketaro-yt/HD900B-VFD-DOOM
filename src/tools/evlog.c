/* SPDX-License-Identifier: GPL-3.0-or-later */
/* evlog SECS : read-only logger for /dev/input/event0..2 (EV_KEY only; other event types are counted).
 * line: <ticks since start (10ms)> ev<N> code=<n> val=<0 up|1 down|2 repeat>      time base = times() (gettimeofday is frozen on this device) */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/times.h>
#include <stdint.h>
struct ie { uint32_t sec, usec; uint16_t type, code; int32_t value; };
int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 10, fd[3], i; long other[3] = {0, 0, 0}; struct tms tm; clock_t t0, now;
    for (i = 0; i < 3; i++) { char p[32]; snprintf(p, sizeof p, "/dev/input/event%d", i); fd[i] = open(p, O_RDONLY | O_NONBLOCK); if (fd[i] < 0) perror(p); }
    t0 = times(&tm); setvbuf(stdout, NULL, _IOLBF, 0);
    printf("evlog start %d s\n", secs);
    for (;;) {
        struct pollfd pf[3]; int n = 0, map[3], r;
        now = times(&tm); if ((now - t0) >= (clock_t)secs * 100) break;
        for (i = 0; i < 3; i++) if (fd[i] >= 0) { pf[n].fd = fd[i]; pf[n].events = POLLIN; map[n++] = i; }
        r = poll(pf, n, 200);
        if (r <= 0) continue;
        for (i = 0; i < n; i++) if (pf[i].revents & POLLIN) {
            struct ie e; while (read(pf[i].fd, &e, sizeof e) == (ssize_t)sizeof e) {
                if (e.type == 1) printf("%ld ev%d code=%u val=%d\n", (long)(times(&tm) - t0), map[i], e.code, e.value);
                else other[map[i]]++;
            }
        }
    }
    printf("evlog end; non-key events: ev0=%ld ev1=%ld ev2=%ld\n", other[0], other[1], other[2]);
    return 0;
}
