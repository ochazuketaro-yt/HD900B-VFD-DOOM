// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "p_local.h"
#include "r_state.h"
#include <math.h>
#include <sys/times.h>
#include "g_game.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

//#define CMAP256

struct fb_var_screeninfo fb = {};
int fb_scaling = 1;
int usemouse = 0;

struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

static struct color colors[256];

// The screen buffer; this is modified to draw things to the screen

byte *I_VideoBuffer = NULL;
byte *I_VideoBuffer_FB = NULL;

/* framebuffer file descriptor */
int fd_fb = 0;

int	X_width;
int X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
	byte r;
	byte g;
	byte b;
} col_t;

// Palette converted to RGB565

static uint16_t rgb565_palette[256];

void cmap_to_rgb565(uint16_t * out, uint8_t * in, int in_pixels)
{
    int i, j;
    struct color c;
    uint16_t r, g, b;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in]; 
        r = ((uint16_t)(c.r >> 3)) << 11;
        g = ((uint16_t)(c.g >> 2)) << 5;
        b = ((uint16_t)(c.b >> 3)) << 0;
        *out = (r | g | b);

        in++;
        for (j = 0; j < fb_scaling; j++) {
            out++;
        }
    }
}

void cmap_to_fb(uint8_t * out, uint8_t * in, int in_pixels)
{
    int i, j, k;
    struct color c;
    uint32_t pix;
    uint16_t r, g, b;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in];  /* R:8 G:8 B:8 format! */
        r = (uint16_t)(c.r >> (8 - fb.red.length));
        g = (uint16_t)(c.g >> (8 - fb.green.length));
        b = (uint16_t)(c.b >> (8 - fb.blue.length));
        pix = r << fb.red.offset;
        pix |= g << fb.green.offset;
        pix |= b << fb.blue.offset;
        if (fb.transp.length == 8) pix |= 0xffu << fb.transp.offset;

        for (k = 0; k < fb_scaling; k++) {
            for (j = 0; j < fb.bits_per_pixel/8; j++) {
                *out = (pix >> (j*8));
                out++;
            }
        }
        in++;
    }
}

void I_InitGraphics (void)
{
    int i;

    /* Open fbdev file descriptor */
    if (getenv("FBDOOM_NOFB")) { fb.xres = 320; fb.yres = 200; fb.bits_per_pixel = 32; fd_fb = -1; }
    else fd_fb = open("/dev/graphics/fb0", O_RDWR);
    if (fd_fb < 0 && !getenv("FBDOOM_NOFB"))
    {
        printf("Could not open /dev/fb0");
        exit(-1);
    }

    /* fetch framebuffer info */
    if (fd_fb >= 0) ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb);
    /* change params if needed */
    //ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb);
    printf("I_InitGraphics: framebuffer: x_res: %d, y_res: %d, x_virtual: %d, y_virtual: %d, bpp: %d, grayscale: %d\n",
            fb.xres, fb.yres, fb.xres_virtual, fb.yres_virtual, fb.bits_per_pixel, fb.grayscale);

    printf("I_InitGraphics: framebuffer: RGBA: %d%d%d%d, red_off: %d, green_off: %d, blue_off: %d, transp_off: %d\n",
            fb.red.length, fb.green.length, fb.blue.length, fb.transp.length, fb.red.offset, fb.green.offset, fb.blue.offset, fb.transp.offset);

    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);


    i = M_CheckParmWithArgs("-scaling", 1);
    if (i > 0) {
        i = atoi(myargv[i + 1]);
        fb_scaling = i;
        printf("I_InitGraphics: Scaling factor: %d\n", fb_scaling);
    } else {
        fb_scaling = fb.xres / SCREENWIDTH;
        if (fb.yres / SCREENHEIGHT < fb_scaling)
            fb_scaling = fb.yres / SCREENHEIGHT;
        printf("I_InitGraphics: Auto-scaling factor: %d\n", fb_scaling);
    }


    /* Allocate screen to draw to */
	I_VideoBuffer = (byte*)Z_Malloc (SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);  // For DOOM to draw on
	I_VideoBuffer_FB = (byte*)malloc(fb.xres * fb.yres * (fb.bits_per_pixel/8));     // For a single write() syscall to fbdev

	screenvisible = true;

    extern int I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void)
{
	Z_Free (I_VideoBuffer);
	free(I_VideoBuffer_FB);
}

void I_StartFrame (void)
{

}

__attribute__ ((weak)) void I_GetEvent (void)
{
//	event_t event;
//	bool button_state;
//
//	button_state = button_read ();
//
//	if (last_button_state != button_state)
//	{
//		last_button_state = button_state;
//
//		event.type = last_button_state ? ev_keydown : ev_keyup;
//		event.data1 = KEY_FIRE;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		D_PostEvent (&event);
//	}
//
//	touch_main ();
//
//	if ((touch_state.x != last_touch_state.x) || (touch_state.y != last_touch_state.y) || (touch_state.status != last_touch_state.status))
//	{
//		last_touch_state = touch_state;
//
//		event.type = (touch_state.status == TOUCH_PRESSED) ? ev_keydown : ev_keyup;
//		event.data1 = -1;
//		event.data2 = -1;
//		event.data3 = -1;
//
//		if ((touch_state.x > 49)
//		 && (touch_state.x < 72)
//		 && (touch_state.y > 104)
//		 && (touch_state.y < 143))
//		{
//			// select weapon
//			if (touch_state.x < 60)
//			{
//				// lower row (5-7)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '5';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '6';
//				}
//				else
//				{
//					event.data1 = '1';
//				}
//			}
//			else
//			{
//				// upper row (2-4)
//				if (touch_state.y < 119)
//				{
//					event.data1 = '2';
//				}
//				else if (touch_state.y < 131)
//				{
//					event.data1 = '3';
//				}
//				else
//				{
//					event.data1 = '4';
//				}
//			}
//		}
//		else if (touch_state.x < 40)
//		{
//			// button bar at bottom screen
//			if (touch_state.y < 40)
//			{
//				// enter
//				event.data1 = KEY_ENTER;
//			}
//			else if (touch_state.y < 80)
//			{
//				// escape
//				event.data1 = KEY_ESCAPE;
//			}
//			else if (touch_state.y < 120)
//			{
//				// use
//				event.data1 = KEY_USE;
//			}
//			else if (touch_state.y < 160)
//			{
//				// map
//				event.data1 = KEY_TAB;
//			}
//			else if (touch_state.y < 200)
//			{
//				// pause
//				event.data1 = KEY_PAUSE;
//			}
//			else if (touch_state.y < 240)
//			{
//				// toggle run
//				if (touch_state.status == TOUCH_PRESSED)
//				{
//					run = !run;
//
//					event.data1 = KEY_RSHIFT;
//
//					if (run)
//					{
//						event.type = ev_keydown;
//					}
//					else
//					{
//						event.type = ev_keyup;
//					}
//				}
//				else
//				{
//					return;
//				}
//			}
//			else if (touch_state.y < 280)
//			{
//				// save
//				event.data1 = KEY_F2;
//			}
//			else if (touch_state.y < 320)
//			{
//				// load
//				event.data1 = KEY_F3;
//			}
//		}
//		else
//		{
//			// movement/menu navigation
//			if (touch_state.x < 100)
//			{
//				if (touch_state.y < 100)
//				{
//					event.data1 = KEY_STRAFE_L;
//				}
//				else if (touch_state.y < 220)
//				{
//					event.data1 = KEY_DOWNARROW;
//				}
//				else
//				{
//					event.data1 = KEY_STRAFE_R;
//				}
//			}
//			else if (touch_state.x < 180)
//			{
//				if (touch_state.y < 160)
//				{
//					event.data1 = KEY_LEFTARROW;
//				}
//				else
//				{
//					event.data1 = KEY_RIGHTARROW;
//				}
//			}
//			else
//			{
//				event.data1 = KEY_UPARROW;
//			}
//		}
//
//		D_PostEvent (&event);
//	}
}

__attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void)
{
}


/* ---- HD900B VFD output: 320x200 -> 45x7 (2 planes: dim/bright), written to $FBDOOM_VFD (default /tmp/vfd/frame) ---- */

/* autopilot (FBDOOM_AUTO): hold forward, turn left/right in a fixed cycle, so a demo needs no keyboard */
static double ap_c, ap_l, ap_r, ap_x, ap_y;
static double ap_ed = 99999.0; static int ap_ecx = -1;
static int nc = 45;   /* columns actually rendered (FBDOOM_COLS: 45 / 40 = 2 groups / 20 = 1 group); rest stays blank */

static int ap_state = 0, ap_t = 0;
static void ap_key(int k, int down) { event_t e; e.type = down ? ev_keydown : ev_keyup; e.data1 = k; e.data2 = 0; D_PostEvent(&e); }
static void autopilot(void)
{   /* steer by the ray distances: turn away from near walls, otherwise walk forward; aim and fire at visible enemies */
    static int cf = 0, ct = 0, hold = 0, cfire = 0;
    int wf, wt, want = 0;
    if (!getenv("FBDOOM_AUTO")) return;
    if (hold > 0) hold--;
    if (ap_ecx >= 0) {                                                       /* enemy visible */
        int err = ap_ecx - nc / 2;
        ct = err < -1 ? -1 : err > 1 ? 1 : 0; want = (err >= -1 && err <= 1); hold = 2;
        wf = (ap_ed > 200.0 && ap_c > 90.0) ? 1 : 0;
    } else {
        { static int n; static double lx, ly; static int esc;
          if (++n >= 30) { n = 0; if (cf && ap_x - lx < 4.0 && lx - ap_x < 4.0 && ap_y - ly < 4.0 && ly - ap_y < 4.0) esc = 20; lx = ap_x; ly = ap_y; }
          if (esc > 0) { esc--; if (ct == 0) { ct = 1; } hold = 2; if (esc == 0) ct = 0; } }
        if (ct == 0 && ap_c < 130.0) { ct = ap_l > ap_r ? -1 : 1; hold = 12; }       /* start turning away from the wall */
        else if (ct != 0 && hold == 0 && ap_c > 260.0) ct = 0;                         /* stop turning once the way is open */
        else if (ct == 0 && ap_l < 70.0 && ap_r > ap_l) { ct = 1; hold = 3; }          /* nudge off side walls */
        else if (ct == 0 && ap_r < 70.0 && ap_l > ap_r) { ct = -1; hold = 3; }
        wf = (ap_c > 90.0) ? 1 : 0;
    }
    wt = ct;
    if (want != cfire) { cfire = want; ap_key(KEY_FIRE, cfire); }
    if (wf != cf) { cf = wf; ap_key(KEY_UPARROW, cf); }
    if (wt != ap_state) {
        if (ap_state) ap_key(ap_state > 0 ? KEY_RIGHTARROW : KEY_LEFTARROW, 0);
        ap_state = wt;
        if (ap_state) ap_key(ap_state > 0 ? KEY_RIGHTARROW : KEY_LEFTARROW, 1);
    }
    (void)ap_t;
}

/* ---- plan A: native 45x7 rendering. 45 rays against the map -> wall height bars (distance), things as blinking blocks.
 * out[0..44] = dim plane (shown every other pass), out[45..89] = bright plane. bit0 = top row.            */
#define RAYCOLS 45
#define PLANE 45
#define RAYMAX  1800.0
#define ROWK    230.0

static int ray_frame; static char sty = 'A'; static int sty_age;
static double ray_hit(double ox, double oy, double dx, double dy)
{
    double best = RAYMAX; int i;
    for (i = 0; i < numlines; i++) {
        line_t *l = &lines[i];
        double x1 = l->v1->x / 65536.0, y1 = l->v1->y / 65536.0, ex = l->dx / 65536.0, ey = l->dy / 65536.0;
        double den = dx * ey - dy * ex, t, u;
        if (den > -1e-9 && den < 1e-9) continue;
        t = ((x1 - ox) * ey - (y1 - oy) * ex) / den;     /* distance along ray */
        u = ((x1 - ox) * dy - (y1 - oy) * dx) / den;     /* position along segment */
        if (t < 1.0 || t >= best || u < 0.0 || u > 1.0) continue;
        if (l->backsector) {                              /* two-sided: solid only if player cannot pass */
            sector_t *f = l->frontsector, *b = l->backsector;
            fixed_t fl = f->floorheight > b->floorheight ? f->floorheight : b->floorheight;
            fixed_t cl = f->ceilingheight < b->ceilingheight ? f->ceilingheight : b->ceilingheight;
            fixed_t step = f->floorheight > b->floorheight ? f->floorheight - b->floorheight : b->floorheight - f->floorheight;
            if (cl - fl >= 56 * FRACUNIT && step <= 24 * FRACUNIT) continue;
        }
        best = t;
    }
    return best;
}

/* visible enemies -> screen columns. returns count; fills ecx (centre col), ew (width), ed (perp distance), eh (height units) */
#define MAXEN 16
static double as_ang = 9.0;   /* signed angle (rad, + = left) of the visible enemy closest to the crosshair, for the aim assist */
static int ray_enemies(double ox, double oy, double a, const double *dist, int *ecx, int *ew, double *ed, double *eh)
{
    thinker_t *th; int n = 0, i; double best = 1e9;
    ap_ecx = -1; ap_ed = 99999.0; as_ang = 9.0;
    for (th = thinkercap.next; th != &thinkercap && n < MAXEN; th = th->next) {
        mobj_t *mo; double dx, dy, ang, d; int cx, w;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mo = (mobj_t *)th;
        if (!((mo->flags & MF_COUNTKILL) && mo->health > 0) || (mo->flags & MF_MISSILE)) continue;
        dx = mo->x / 65536.0 - ox; dy = mo->y / 65536.0 - oy;
        ang = atan2(dy, dx) - a; while (ang > 3.14159265) ang -= 6.28318531; while (ang < -3.14159265) ang += 6.28318531;
        if (ang > 0.79 || ang < -0.79) continue;
        d = sqrt(dx * dx + dy * dy) * cos(ang);
        if (d < 8.0) continue;
        cx = (int)((1.0 - tan(ang)) * 0.5 * nc);
        w = (int)((mo->radius / 65536.0) / d * nc + 0.5); if (w < 1) w = 1; if (w > 6) w = 6;
        if (cx < 0 || cx >= nc || d > dist[cx] + 20.0) continue;                   /* hidden behind a nearer wall */
        if (fabs(ang) < fabs(as_ang)) as_ang = ang;
        ecx[n] = cx; ew[n] = w; ed[n] = d; eh[n] = mo->height / 65536.0;
        if (d < best) { best = d; ap_ecx = cx; ap_ed = d; }
        n++;
    }
    (void)i; return n;
}

/* ---- UI: boot logo, New Game / Exit menu (FBDOOM_UI=1). state 0 = play, 1 = logo, 2 = menu. input side: i_input_tty.c ---- */
int vfd_ui_state = 0, vfd_menu_sel = 0, vfd_game_started = 0;
int vfd_god = 0, vfd_spawn = 0;                 /* runtime game mode: ORIG (0,0) / WAVE (1,1) / FREE (1,0); initial values from FBDOOM_GOD / FBDOOM_NOSPAWN */
static const char *const ui_items[4] = {"ORIG", "WAVE", "FREE", "EXIT"};
static const unsigned ui_itembar[4] = {0x0007, 0x0078, 0x0780, 0x3800};
static long ui_t0 = -1;
static void ui_bar(unsigned char *sym, int n, unsigned mask)
{   /* lower bar: block b (0..13) = byte b%5, bit 3+b/5.  n = first n blocks, or mask = explicit block bits */
    int b; memset(sym, 0, 5);
    for (b = 0; b < 14; b++) if (mask ? (mask >> b) & 1 : b < n) sym[b % 5] |= 1 << (3 + b / 5);
}
static const unsigned char *ui_glyph(char c)
{
    static const unsigned char G_[] = {0x3e,0x41,0x49,0x49,0x7a}, A_[] = {0x7e,0x11,0x11,0x11,0x7e}, M_[] = {0x7f,0x02,0x0c,0x02,0x7f}, E_[] = {0x7f,0x49,0x49,0x49,0x41},
        X_[] = {0x63,0x14,0x08,0x14,0x63}, R_[] = {0x7f,0x09,0x19,0x29,0x46}, W_[] = {0x3f,0x40,0x38,0x40,0x3f}, V_[] = {0x1f,0x20,0x40,0x20,0x1f}, F_[] = {0x7f,0x09,0x09,0x09,0x01}, I_[] = {0x00,0x41,0x7f,0x41,0x00}, T_[] = {0x01,0x01,0x7f,0x01,0x01}, D_[] = {0x7f,0x41,0x41,0x22,0x1c}, O_[] = {0x3e,0x41,0x41,0x41,0x3e};
    switch (c) { case 'G': return G_; case 'A': return A_; case 'M': return M_; case 'E': return E_; case 'X': return X_; case 'R': return R_; case 'W': return W_; case 'V': return V_; case 'F': return F_; case 'I': return I_; case 'T': return T_; case 'D': return D_; default: return O_; }
}
static void ui_text(unsigned char *out, const char *t, int nshow)
{
    int i; for (i = 0; i < 4 && i < nshow && t[i]; i++) memcpy(out + PLANE + 5 * i, ui_glyph(t[i]), 5);
}
static const char *const ui_logo[7] = {"###...##...##..##.##","#++#.#++#.#++#.#####","#++#.#++#.#++#.#.#.#","#++#.#++#.#++#.#.#.#","#++#.#++#.#++#.#...#","#++#.#++#.#++#.#...#","###...##...##..#...#"};
static int ui_render(unsigned char *out)
{
    struct tms tm; long now = (long)times(&tm), e;
    if (ui_t0 < 0) { ui_t0 = now; vfd_ui_state = 1; }
    e = now - ui_t0;
    if (vfd_ui_state == 1 && e >= 450) vfd_ui_state = 2;
    if (vfd_ui_state != 0) paused = true;                        /* the game waits behind the logo / menu */
    if (vfd_ui_state == 0) return 0;
    memset(out, 0, 140);
    if (vfd_ui_state == 1) {                                     /* logo: 20x7 image of the DOOM logo; '#' bright, '+' grey (dim plane). wipe in from the left, flash, hold; bar sweeps */
        int x, y, reveal = (int)(e / 4) + 1, flash = (e >= 200 && e < 320 && ((e / 8) & 1));
        for (x = 0; x < 20 && x < reveal; x++) for (y = 0; y < 7; y++) {
            char c = ui_logo[y][x];
            if (c == '#' || (c == '+' && flash)) out[PLANE + x] |= 1 << y;
            else if (c == '+') out[x] |= 1 << y;
        }
        ui_bar(out + 135, e / 18 > 14 ? 14 : (int)(e / 18), 0);
    } else {
        ui_text(out, ui_items[vfd_menu_sel & 3], 4);
        ui_bar(out + 135, 0, ui_itembar[vfd_menu_sel & 3]);        /* bar position = item (ORIG left ... EXIT right) */
    }
    return 1;
}
static int vfd_ray(unsigned char *out)
{
    player_t *pl; mobj_t *me; double ox, oy, a, dist[RAYCOLS], ph[RAYCOLS], rowsf; int x, y; thinker_t *th;
    { static int dc; if (getenv("FBDOOM_DBG") && ++dc % 10 == 0 && dc < 400) fprintf(stderr, "DBG0 n=%d gamestate=%d gametic=%d lines=%p mo=%p\n", dc, (int)gamestate, gametic, (void*)lines, (void*)players[consoleplayer].mo); }
    if (gamestate != GS_LEVEL) return 0;
    pl = &players[consoleplayer]; me = pl->mo;
    if (!me || !lines) return 0;
    ox = me->x / 65536.0; oy = me->y / 65536.0; a = me->angle * (6.283185307179586 / 4294967296.0);
    memset(out, 0, 140);
    ray_frame++;
    { static int mi; if (!mi) { mi = 1; vfd_god = getenv("FBDOOM_GOD") != NULL; vfd_spawn = vfd_god && !getenv("FBDOOM_NOSPAWN"); } }
    if (vfd_god) { pl->cheats |= CF_GODMODE; pl->health = 100; if (pl->ammo[am_clip] < 20) pl->ammo[am_clip] = 50; }
    if (vfd_spawn && ray_frame % 120 == 60) {   /* fight demo: keep a zombieman about 350 units ahead whenever none is near */
        thinker_t *t3; int near = 0; double dx2 = cos(a), dy2 = sin(a);
        for (t3 = thinkercap.next; t3 != &thinkercap; t3 = t3->next) {
            mobj_t *m3; double ex, ey; if (t3->function.acp1 != (actionf_p1)P_MobjThinker) continue; m3 = (mobj_t *)t3;
            if (!((m3->flags & MF_COUNTKILL) && m3->health > 0)) continue;
            ex = m3->x / 65536.0 - ox; ey = m3->y / 65536.0 - oy; if (ex * ex + ey * ey < 700.0 * 700.0) near = 1;
        }
        if (!near && ray_hit(ox, oy, dx2, dy2) > 500.0) {
            P_SpawnMobj(me->x + (fixed_t)(dx2 * 350.0 * 65536.0), me->y + (fixed_t)(dy2 * 350.0 * 65536.0), ONFLOORZ, MT_POSSESSED);
            if (getenv("FBDOOM_STAT")) fprintf(stderr, "SPAWN f=%d\n", ray_frame);
        }
    }
    { static int ni; if (!ni) { ni = 1; if (getenv("FBDOOM_COLS")) { nc = atoi(getenv("FBDOOM_COLS")); if (nc < 5) nc = 5; if (nc > 45) nc = 45; } } }
    {   static const char *sl; static int per = 900;
        if (!sl) { sl = getenv("FBDOOM_STYLE"); if (!sl) sl = "A"; if (getenv("FBDOOM_STYLE_FRAMES")) per = atoi(getenv("FBDOOM_STYLE_FRAMES")); }
        sty = sl[(ray_frame / per) % strlen(sl)]; sty_age = ray_frame % per;
    }
    if (getenv("FBDOOM_DBG") && ray_frame % 5 == 0) fprintf(stderr, "DBG f=%d gametic=%d pos=%.0f,%.0f ang=%.2f menu=%d paused=%d\n", ray_frame, gametic, ox, oy, a, menuactive, paused);
    for (x = 0; x < nc; x++) {
        double tn = 1.0 - 2.0 * (x + 0.5) / nc, phi = atan(tn), d;
        d = ray_hit(ox, oy, cos(a + phi), sin(a + phi));
        dist[x] = d * cos(phi); ph[x] = phi;
        if (d >= RAYMAX) { dist[x] = 99999.0; continue; }
        if (sty != 'A') continue;
        rowsf = 7.0 * ROWK / dist[x];
        for (y = 0; y < 7; y++) {
            double c = rowsf / 2.0 - (y > 3 ? y - 3 : 3 - y) + 0.5;
            if (c >= 0.75) out[PLANE + x] |= 1 << y; else if (c >= 0.25) out[x] |= 1 << y;
        }
    }
    if (getenv("FBDOOM_DBG") && ray_frame % 20 == 0) fprintf(stderr, "DBG R f=%d pos=%.0f,%.0f a=%.2f d0=%.0f d11=%.0f d22=%.0f d33=%.0f d44=%.0f nl=%d\n", ray_frame, ox, oy, a, dist[0], dist[11], dist[22], dist[33], dist[44], numlines);
    { double c = 9999, l = 0, r = 0; int k; int k0 = nc * 17 / 45, k1 = nc * 27 / 45, nl = nc / 3, nr = nc - nc * 2 / 3; for (k = k0; k <= k1; k++) if (dist[k] < c) c = dist[k]; for (k = 0; k < nl; k++) l += dist[k]; for (k = nc * 2 / 3; k < nc; k++) r += dist[k]; ap_c = c; ap_l = l / nl; ap_r = r / nr; ap_x = ox; ap_y = oy; }
    if (getenv("FBDOOM_DIST") && ray_frame % 12 == 0) { int k; fprintf(stderr, "DIST"); for (k = 0; k < nc; k++) fprintf(stderr, " %.0f", dist[k]); fprintf(stderr, "\n"); }
    if (sty != 'A') {                                                  /* B/G/D: outline & radar styles, bright plane only */
        int t[RAYCOLS], b[RAYCOLS], k, yy, y0, y1, xx;
        memset(out, 0, 140);
        for (x = 0; x < nc; x++) {
            double d = dist[x], h;
            if (sty == 'D') { y = 6 - (int)(d / 200.0); if (y < 0) y = 0; if (y > 6) y = 6; if (d > 9999) y = 0; t[x] = b[x] = y; continue; }
            if (d > 9999) { t[x] = b[x] = 3; continue; }
            if (sty == 'B') { h = 7.0 * ROWK / d / 2.0; if (h > 3.5) h = 3.5; t[x] = (int)floor(3.0 - h + 0.5 + 0.5); b[x] = (int)floor(3.0 + h - 0.5 + 0.5); }
            else { h = 3.4 * (1.0 - log((d < 70.0 ? 70.0 : d) / 70.0) / log(1800.0 / 70.0)); if (h < 0) h = 0; t[x] = (int)floor(3.0 - h + 0.5); b[x] = (int)floor(3.0 + h + 0.5); }
        }
        for (x = 0; x < nc; x++) {
            if (t[x] >= 0 && t[x] < 7) out[PLANE + x] |= 1 << t[x];
            if (b[x] >= 0 && b[x] < 7) out[PLANE + x] |= 1 << b[x];
            if (x < nc - 1 && dist[x] < 9999 && dist[x + 1] < 9999) {
                double dn = dist[x] < dist[x + 1] ? dist[x] : dist[x + 1];
                int dt = t[x] > t[x + 1] ? t[x] - t[x + 1] : t[x + 1] - t[x];
                double dd = dist[x] > dist[x + 1] ? dist[x] - dist[x + 1] : dist[x + 1] - dist[x];
                if (sty == 'D' ? (dt >= 2) : (dt >= 2 || dd > 0.35 * dn)) {
                    xx = dist[x] < dist[x + 1] ? x : x + 1;
                    y0 = t[x] < t[x + 1] ? t[x] : t[x + 1]; y1 = b[x] > b[x + 1] ? b[x] : b[x + 1];
                    if (sty == 'D') { y0 = t[x] < t[x + 1] ? t[x] : t[x + 1]; y1 = t[x] > t[x + 1] ? t[x] : t[x + 1]; xx = x; }
                    if (sty != 'D') for (yy = y0; yy <= y1; yy++) if (yy >= 0 && yy < 7) out[PLANE + xx] |= 1 << yy;
                }
            }
        }
        if (sty == 'B' || sty == 'G') {                                   /* enemies (solid bars standing on the floor line) + gun */
            int ecx[MAXEN], ew[MAXEN], ne, e, c, rows, wr;
            double ed[MAXEN], eh[MAXEN];
            ne = ray_enemies(ox, oy, a, dist, ecx, ew, ed, eh);
            if (getenv("FBDOOM_ASSIST") && !getenv("FBDOOM_AUTO") && vfd_ui_state == 0 && !paused && as_ang > -0.22 && as_ang < 0.22)
                me->angle += (angle_t)(int)(as_ang * 0.2 * (4294967296.0 / 6.283185307179586));   /* aim assist: pull the view 20%/frame toward an enemy within ~12 deg */
            if (getenv("FBDOOM_STAT")) {
                static int sf, sv, sfl, sfire; static double smin = 99999.0;
                thinker_t *t2; int alive = 0, fov = 0;
                sf++; if (ne > 0) { sv++; if (ap_ed < smin) smin = ap_ed; }
                if (pl->psprites[ps_flash].state) sfl++;
                if (pl->psprites[ps_weapon].state && pl->attackdown) sfire++;
                if (sf % 100 == 0) {
                    for (t2 = thinkercap.next; t2 != &thinkercap; t2 = t2->next) {
                        mobj_t *m2; if (t2->function.acp1 != (actionf_p1)P_MobjThinker) continue; m2 = (mobj_t *)t2;
                        if ((m2->flags & MF_COUNTKILL) && m2->health > 0) { alive++;
                            { double ddx = m2->x/65536.0-ox, ddy = m2->y/65536.0-oy; if (ddx*ddx+ddy*ddy < 640000.0) fov++; } }
                    }
                    fprintf(stderr, "STAT f=%d tic=%d vis=%d flash=%d atk=%d mind=%.0f alive=%d near800=%d kills=%d ammo=%d hp=%d pos=%.0f,%.0f\n",
                        sf, gametic, sv, sfl, sfire, smin, alive, fov, pl->killcount, pl->ammo[am_clip], pl->health, ox, oy);
                }
            }
            for (e = 0; e < ne; e++) {
                int c0 = ecx[e] - 1, bb = b[ecx[e]] > 6 ? 6 : b[ecx[e]];
                double dd = ed[e] < 70.0 ? 70.0 : ed[e];
                /* height from distance: 6 rows at point blank, 1 row far away (log mapping like the walls) */
                rows = (int)(6.0 * (1.0 - log(dd / 70.0) / log(1800.0 / 70.0)) + 0.5); if (rows < 1) rows = 1; if (rows > 6) rows = 6;
                if (bb < rows - 1) bb = rows - 1;
                for (c = c0; c < c0 + 2; c++) {
                    if (c < 0 || c >= nc) continue;
                    out[PLANE + c] = 0;                                      /* blank the wall outline under the enemy */
                    for (yy = bb - rows + 1; yy <= bb; yy++) if (yy >= 0 && yy < 7) out[2 * PLANE + c] |= 1 << yy;   /* blink plane: vfdmux toggles it */
                }
            }
            {   int cc = nc / 2;                                              /* gun: 3 wide base + barrel, flash = muzzle burst */
                out[PLANE + cc - 1] = (out[PLANE + cc - 1] & ~0x60) | 0x40;
                out[PLANE + cc]     = (out[PLANE + cc] & ~0x78) | 0x60;
                out[PLANE + cc + 1] = (out[PLANE + cc + 1] & ~0x60) | 0x40;
                {   static int fst = -1, fon;
                    if (pl->psprites[ps_flash].state) { if (!fon) { fon = 1; fst = gametic; } } else fon = 0;
                    if (fon && gametic - fst < 4)                                  /* muzzle flash = whole display lit, first 4 tics (~half of the pistol flash) */
                        for (c = 0; c < nc; c++) { out[PLANE + c] = 0x7f; out[2 * PLANE + c] = 0; }
                }
            }
        }
        if (getenv("FBDOOM_HUD")) ui_bar(out + 135, pl->health <= 0 ? 0 : (pl->health * 14 + 99) / 100, 0);   /* health on the lower 14-block bar */
        return 1;
    }
    for (th = thinkercap.next; th != &thinkercap; th = th->next) {          /* things in front of walls */
        mobj_t *mo; double dx, dy, ang, rel, d, wcol, h, bot; int cx, w, c0, c1, c, top, bt, enemy;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mo = (mobj_t *)th;
        if (mo == me || (mo->flags & MF_MISSILE)) continue;
        enemy = (mo->flags & MF_COUNTKILL) && mo->health > 0;
        if (!enemy && !(mo->flags & MF_SPECIAL)) continue;
        dx = mo->x / 65536.0 - ox; dy = mo->y / 65536.0 - oy;
        ang = atan2(dy, dx) - a; while (ang > 3.14159265) ang -= 6.28318531; while (ang < -3.14159265) ang += 6.28318531;
        if (ang > 0.79 || ang < -0.79) continue;
        d = sqrt(dx * dx + dy * dy) * cos(ang);
        if (d < 8.0) continue;
        cx = (int)((1.0 - tan(ang)) * 0.5 * nc);
        w = (int)(2.0 * (mo->radius / 65536.0) / d * 22.5 + 0.5); if (w < 1) w = 1; if (w > 9) w = 9;
        if (enemy) { h = 7.0 * ROWK / d * (mo->height / 65536.0) / 128.0; if (h < 1.0) h = 1.0; }
        else h = 1.0;
        bot = 3.0 + 7.0 * ROWK / d / 2.0; if (bot > 7.0) bot = 7.0;      /* floor line of a wall at this distance */
        bt = (int)(bot + 0.5); top = bt - (int)(h + 0.5); if (top < 0) top = 0; if (bt > 7) bt = 7;
        c0 = cx - w / 2; c1 = c0 + w - 1;
        for (c = c0; c <= c1; c++) {
            if (c < 0 || c >= nc || d > dist[c] + 20.0) continue;       /* hidden behind a nearer wall */
            for (y = 0; y < 7; y++) { out[c] &= ~(1 << y); out[PLANE + c] &= ~(1 << y); }
            for (y = top; y < bt; y++) {
                if (enemy) { if ((ray_frame >> 2) & 1) out[PLANE + c] |= 1 << y; else out[c] |= 1 << y; }   /* blinks bright/dim */
                else out[c] |= 1 << y;                                                                      /* pickup: dim dot */
            }
        }
    }
    (void)ph;
    return 1;
}
static int vfd_fd = -2;
static void vfd_emit(void)
{
    unsigned char out[140]; int cx, cy, x, y, lum[45][7], lo = 255, hi = 0;
    if (vfd_fd == -2) {
        const char *pth = getenv("FBDOOM_VFD");
        vfd_fd = pth ? open(pth, O_WRONLY | O_CREAT, 0666) : -1;
    }
    if (vfd_fd < 0) return;
    memset(out, 0, sizeof out);
    if (getenv("FBDOOM_UI") && ui_render(out)) { pwrite(vfd_fd, out, sizeof out, 0); return; }
    if (getenv("FBDOOM_RAY") && vfd_ray(out)) { pwrite(vfd_fd, out, sizeof out, 0); return; }
    for (cx = 0; cx < 45; cx++) for (cy = 0; cy < 7; cy++) {
        int x0 = cx * SCREENWIDTH / 45, x1 = (cx + 1) * SCREENWIDTH / 45, y0 = 40 + cy * 12, sum = 0, n = 0;
        for (y = y0; y < y0 + 12; y++) for (x = x0; x < x1; x += 2) {
            struct color c = colors[I_VideoBuffer[y * SCREENWIDTH + x]];
            sum += (c.r * 30 + c.g * 59 + c.b * 11) / 100; n++;
        }
        lum[cx][cy] = sum / n;
        if (lum[cx][cy] < lo) lo = lum[cx][cy];
        if (lum[cx][cy] > hi) hi = lum[cx][cy];
    }
    {   /* contrast stretch between 8th and 92nd percentile, then 2x2 ordered dither into 2 planes (dim/bright) */
        int hist[256] = {0}, acc = 0, n = 0;
        static const int bayer[2][2] = {{0, 2}, {3, 1}};
        for (cx = 0; cx < 45; cx++) for (cy = 0; cy < 7; cy++) hist[lum[cx][cy]]++;
        for (lo = 0; lo < 255 && acc + hist[lo] < 25; lo++) acc += hist[lo];
        acc = 0; for (hi = 255; hi > 0 && acc + hist[hi] < 25; hi--) acc += hist[hi];
        (void)n; if (hi <= lo + 4) hi = lo + 5;
        memset(out, 0, sizeof out);
        for (cx = 0; cx < 45; cx++) for (cy = 0; cy < 7; cy++) {
            int v = (lum[cx][cy] - lo) * 2000 / (hi - lo);          /* 0..2000 = 0..2 levels x1000 */
            v += 0; (void)bayer;   /* dither disabled: it only added checker noise at 45x7 */
            if (v > 500)  out[cx] |= 1 << cy;
            if (v > 1500) out[45 + cx] |= 1 << cy;
        }
    }
    pwrite(vfd_fd, out, sizeof out, 0);
}

//
// I_FinishUpdate
//

void I_FinishUpdate (void)
{
    int y;
    int x_offset, y_offset, x_offset_end;
    unsigned char *line_in, *line_out;

    autopilot();
    vfd_emit();
    if (getenv("FBDOOM_NOFB")) return;

    /* Offsets in case FB is bigger than DOOM */
    /* 600 = fb heigt, 200 screenheight */
    /* 600 = fb heigt, 200 screenheight */
    /* 2048 =fb width, 320 screenwidth */
    y_offset     = (((fb.yres - (SCREENHEIGHT * fb_scaling)) * fb.bits_per_pixel/8)) / 2;
    x_offset     = (((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8)) / 2; // XXX: siglent FB hack: /4 instead of /2, since it seems to handle the resolution in a funny way
    //x_offset     = 0;
    x_offset_end = ((fb.xres - (SCREENWIDTH  * fb_scaling)) * fb.bits_per_pixel/8) - x_offset;

    /* DRAW SCREEN */
    line_in  = (unsigned char *) I_VideoBuffer;
    line_out = (unsigned char *) I_VideoBuffer_FB;

    y = SCREENHEIGHT;

    while (y--)
    {
        int i;
        for (i = 0; i < fb_scaling; i++) {
            line_out += x_offset;
#ifdef CMAP256
            for (fb_scaling == 1) {
                memcpy(line_out, line_in, SCREENWIDTH); /* fb_width is bigger than Doom SCREENWIDTH... */
            } else {
                //XXX FIXME fb_scaling support!
            }
#else
            //cmap_to_rgb565((void*)line_out, (void*)line_in, SCREENWIDTH);
            cmap_to_fb((void*)line_out, (void*)line_in, SCREENWIDTH);
#endif
            line_out += (SCREENWIDTH * fb_scaling * (fb.bits_per_pixel/8)) + x_offset_end;
        }
        line_in += SCREENWIDTH;
    }

    /* Start drawing from y-offset */
    lseek(fd_fb, y_offset * fb.xres, SEEK_SET);
    write(fd_fb, I_VideoBuffer_FB, (SCREENHEIGHT * fb_scaling * (fb.bits_per_pixel/8)) * fb.xres); /* draw only portion used by doom + x-offsets */
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
	int i;
	//col_t* c;

	//for (i = 0; i < 256; i++)
	//{
	//	c = (col_t*)palette;

	//	rgb565_palette[i] = GFX_RGB565(gammatable[usegamma][c->r],
	//								   gammatable[usegamma][c->g],
	//								   gammatable[usegamma][c->b]);

	//	palette += 3;
	//}
    

    /* performance boost:
     * map to the right pixel format over here! */

    for (i=0; i<256; ++i ) {
        colors[i].a = 0;
        colors[i].r = gammatable[usegamma][*palette++];
        colors[i].g = gammatable[usegamma][*palette++];
        colors[i].b = gammatable[usegamma][*palette++];
    }

    /* Set new color map in kernel framebuffer driver */
    //XXX FIXME ioctl(fd_fb, IOCTL_FB_PUTCMAP, colors);
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
    int best, best_diff, diff;
    int i;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
    	color.r = GFX_RGB565_R(rgb565_palette[i]);
    	color.g = GFX_RGB565_G(rgb565_palette[i]);
    	color.b = GFX_RGB565_B(rgb565_palette[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_BeginRead (void)
{
}

void I_EndRead (void)
{
}

void I_SetWindowTitle (char *title)
{
}

void I_GraphicsCheckCommandLine (void)
{
}

void I_SetGrabMouseCallback (grabmouse_callback_t func)
{
}

void I_EnableLoadingDisk(void)
{
}

void I_BindVideoVariables (void)
{
}

void I_DisplayFPSDots (boolean dots_on)
{
}

void I_CheckIsScreensaver (void)
{
}
