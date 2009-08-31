/* xenos.c - pseudo framebuffer driver for Xenos on Xbox 360
 *
 * Copyright (C) 2007, 2008 Georg Lukas <georg@boerde.de>
 *
 * Based on code by Felix Domke <tmbinc@elitedvb.net>
 *
 * This software is provided under the GNU GPL License v2.
 */
#include <cache.h>
#include <types.h>
#include <string.h>
#include <vsprintf.h>
#include "version.h"

/* activate NATIVE_RESOLUTION to change the screen resolution and disable
 * scaling. This won't work on 1080i */
//#define NATIVE_RESOLUTION 1

/* reinit xenos from scratch. so far this supports only 640x480 */
#define REINIT_VIDEO 1

/* activate BUFFER_PREINIT to enable storing of the xell output before the
 * xenos framebuffer is initialized. This should help with SMP issues. */
#define BUFFER_PREINIT

int xenos_width, xenos_height,
    xenos_size;

void xenos_lowlevel_init(void);


/* Colors in BGRA: background and foreground */
#ifdef DEFAULT_THEME
uint32_t xenos_color[2] = { 0x00000000, 0xFFC0C000 };
#else
uint32_t xenos_color[2] = { 0xD8444E00, 0xFF96A300 };
#endif

/* can't initialize xenos_fb with zero due to late BSS init,
 * instead init it in xenos_preinit() */
unsigned char *xenos_fb;

int cursor_x, cursor_y,
    max_x, max_y;

struct ati_info {
	uint32_t unknown1[4];
	uint32_t base;
	uint32_t unknown2[8];
	uint32_t width;
	uint32_t height;
};


/* set a pixel to RGB values, must call xenos_init() first */
inline void xenos_pset32(int x, int y, int color)
{
#define fbint ((uint32_t*)xenos_fb)
#define base (((y >> 5)*32*xenos_width + ((x >> 5)<<10) \
	   + (x&3) + ((y&1)<<2) + (((x&31)>>2)<<3) + (((y&31)>>1)<<6)) ^ ((y&8)<<2))
	fbint[base] = color;
#undef fbint
#undef base
}

inline void xenos_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
	xenos_pset32(x, y, (b<<24) + (g<<16) + (r<<8));
	/* little indian:
	 * fbint[base] = b + (g<<8) + (r<<16); */
}

extern const unsigned char fontdata_8x16[4096];

void xenos_draw_char(const int x, const int y, const unsigned char c) {
#define font_pixel(ch, x, y) ((fontdata_8x16[ch*16+y]>>(7-x))&1)
	int lx, ly;
	for (ly=0; ly < 16; ly++)
		for (lx=0; lx < 8; lx++) {
			xenos_pset32(x+lx, y+ly, xenos_color[font_pixel(c, lx, ly)]);
		}
}

void xenos_clrscr(const unsigned int bgra) {
	uint32_t *fb = (uint32_t*)xenos_fb;
	int count = xenos_size;
	while (count--)
		*fb++ = bgra;
	dcache_flush(xenos_fb, xenos_size*4);
}

void xenos_scroll32(const unsigned int lines) {
	int l, bs;
	bs = xenos_width*32*4;
	/* copy all tile blocks to a higher position */
	for (l=lines; l*32 < xenos_height; l++) {
		memcpy(xenos_fb + bs*(l-lines),
		       xenos_fb + bs*l,
		       bs);
	}
	/* fill up last lines with background color */
	uint32_t *fb = (uint32_t*)(xenos_fb + xenos_size*4 - bs*lines);
	uint32_t *end = (uint32_t*)(xenos_fb + xenos_size*4);
	while (fb != end)
		*fb++ = xenos_color[0];

	dcache_flush(xenos_fb, xenos_size*4);
}

void xenos_newline() {
#if 0
	/* fill up with spaces */
	while (cursor_x*8 < xenos_width) {
		xenos_draw_char(cursor_x*8, cursor_y*16, ' ');
		cursor_x++;
	}
#endif
	
	/* reset to the left */
	cursor_x = 0;
	cursor_y++;
	if (cursor_y >= max_y) {
		/* XXX implement scrolling */
		xenos_scroll32(1);
		cursor_y -= 2;
	}
}

static inline void xenos_putch_impl(const char c) {
	if (c == '\t') {
		/* move to the next multiple of 8 */
		cursor_x += 7;
		cursor_x &= ~7;
	} else if (c == '\r') {
		cursor_x = 0;
	} else if (c == '\n') {
		xenos_newline();
	} else {
		xenos_draw_char(cursor_x*8, cursor_y*16, c);
		cursor_x++;
		if (cursor_x >= max_x)
			xenos_newline();
	}
}

#ifdef BUFFER_PREINIT

/* size of the srollback buffer */
#define BUFFER 2048
int bufpos = 0;
unsigned char buffer[BUFFER];

#endif

void xenos_putch(const char c) {
	if (!xenos_fb) {
#ifdef BUFFER_PREINIT
		buffer[bufpos++] = c;
		if (bufpos == BUFFER)
			bufpos--;
#else
		return;
#endif
	} else {
		xenos_putch_impl(c);
		dcache_flush(xenos_fb, xenos_size*4);
	}
}

/* This was added to improve userfriendlyness */
char* xenos_ascii = "\n"
	" ######################################################\n"
	" #        ##########################       ####      ##\n"
	" #  ###############################  #####  ##   ##   #\n"
	" #  ###############################  #########  ####  #\n"
	" #  ########  #   ####   ####   ###       ####  ####  #\n"
	" #      ####    #  ##  #  ##  #  ##   ###  ###  ####  #\n"
	" #  ########  #######     ##     ##  #####  ##  ####  #\n"
	" #  ########  #######  #####  #####  #####  ##  ####  #\n"
	" #  ########  #######  #  ##  #  ###  ###   ##   ##   #\n"
	" #  ########  ########   ####   #####     #####      ##\n"
	" ######################################################\n"
	"           XeLL - Xenon Linux Loader " VERSION "\n\n";

void xenos_asciiart() {
	char *p = xenos_ascii;
	while (*p)
		xenos_putch_impl(*p++);
	dcache_flush(xenos_fb, xenos_size*4);
}

void xenos_init() {
	struct ati_info *ai = (struct ati_info*)0x80000200ec806100ULL;

#if REINIT_VIDEO
	xenos_lowlevel_init();
#elif NATIVE_RESOLUTION
	uint32_t *gfx = (uint32_t*)0x80000200ec806000ULL;

	/* setup native resolution, i.e. disable scaling */
	int vxres = gfx[0x134/4];
	int vyres = gfx[0x138/4];
	int scl_h = gfx[0x5b4/4], scl_v = gfx[0x5c4/4];

	if (gfx[0x590/4] == 0)
		scl_h = scl_v = 0x01000000;

	int interlaced = gfx[0x30/4];
	int black_top = gfx[0x44/4];
	int offset = gfx[0x580/4];
	int offset_x = (offset >> 16) & 0xFFFF;
	int offset_y = offset & 0xFFFF;
	printf(" - virtual resolution: %d x %d\n", vxres, vyres);
	printf(" - offset: x=%d, y=%d\n", offset_x, offset_y);
	printf(" - black: %d %d, %d %d\n",
		gfx[0x44/4], gfx[0x48/4], gfx[0x4c/4], gfx[0x50/4]);
	printf(" - interlace flag: %x\n", interlaced);

	int nxres = (vxres - offset_x * 2) * 0x1000 / (scl_h/0x1000);
	int nyres = (vyres - offset_y * 2) * 0x1000 / (scl_v/0x1000) + black_top * 2;
	printf(" - native resolution: %d x %d\n", nxres, nyres);

	/* do not change res for interlaced mode! */
	if (!interlaced) {
		gfx[0x44/4] = 0; // disable black bar
		gfx[0x48/4] = 0;
		gfx[0x4c/4] = 0;
		gfx[0x50/4] = 0;

		gfx[0x590/4] = 0; // disable scaling
		gfx[0x584/4] = (nxres << 16) | nyres;
		gfx[0x580/4] = 0; // disable offset
		gfx[0x5e8/4] = (nxres * 4) / 0x10 - 1; // fix pitch
		gfx[0x134/4] = nxres;
		gfx[0x138/4] = nyres;
	}
#endif
	xenos_fb = (unsigned char*)((long)(ai->base) | 0x8000000000000000ULL);
	/* round up size to tiles of 32x32 */
	xenos_width = ((ai->width+31)>>5)<<5;
	xenos_height = ((ai->height+31)>>5)<<5;
	xenos_size = xenos_width*xenos_height;

	cursor_x = cursor_y = 0;
	max_x = ai->width / 8;
	max_y = ai->height / 16;

	/* XXX use memset_io() instead? */
	xenos_clrscr(xenos_color[0]);

#ifdef BUFFER_PREINIT
	int pos;
	for (pos = 0; pos < bufpos; pos++)
		xenos_putch_impl(buffer[pos]);
	if (bufpos == BUFFER - 1)
		printf(" * Xenos FB BUFFER overrun!\n");
#endif

	printf(" * Xenos FB with %dx%d (%dx%d) at %p initialized.\n",
		max_x, max_y, ai->width, ai->height, xenos_fb);

	xenos_asciiart();
}

