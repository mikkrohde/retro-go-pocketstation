#ifndef __LCD_H__
#define __LCD_H__

#include "gnuboy.h"

#define GB_WIDTH (160)
#define GB_HEIGHT (144)

typedef struct
{
	byte y;
	byte x;
	byte pat;
	byte flags;
} obj_t;

typedef struct
{
	int pat, x, v, pal, pri;
} vissprite_t;

typedef struct
{
	byte vbank[2][8192];
	union
	{
		byte mem[256];
		obj_t obj[40];
	} oam;
	byte pal[128];

	int BG[64];
	int WND[64];
	byte BUF[0x100];
	byte PRI[0x100];
	vissprite_t VS[16];

	byte pix_buf[8];

	int S, T, U, V;
	int WX, WY, WT, WV;

	int cycles;

	// Fix for Fushigi no Dungeon - Fuurai no Shiren GB2 and Donkey Kong
	int enable_window_offset_hack;
} lcd_t;

enum {
	GB_PIXEL_PALETTED,
	GB_PIXEL_565_LE,
	GB_PIXEL_565_BE,
};

typedef struct
{
	byte *buffer;
	byte *vdest;
	un16 palette[64];
	int format;
	int enabled;
	void (*blit_func)();
} fb_t;

extern lcd_t lcd;
extern fb_t fb;

void lcd_reset(bool hard);
void lcd_emulate();

void lcdc_change(byte b);
void stat_trigger();

void pal_write(byte i, byte b);
void pal_write_dmg(byte i, byte mapnum, byte d);
void pal_dirty();
void pal_set_dmg(int palette);
int  pal_get_dmg();
int  pal_count_dmg();

#endif
