/* SPDX-License-Identifier: GPL-3.0-or-later */
/* tt -- prints gettimeofday / times() / /proc/uptime 8 times: shows that gettimeofday() is frozen on the HD900B firmware */
#include <stdio.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#include <fcntl.h>
int main(void){int i;for(i=0;i<8;i++){struct timeval t;char b[64]={0};int f;gettimeofday(&t,0);f=open("/proc/uptime",O_RDONLY);read(f,b,40);close(f);printf("gtod %ld.%06ld times %ld uptime %s",(long)t.tv_sec,(long)t.tv_usec,(long)times(0),b);fflush(stdout);usleep(300000);}return 0;}
