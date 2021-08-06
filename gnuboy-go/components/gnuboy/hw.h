#pragma once

#include "gnuboy.h"

#define PAD_RIGHT  0x01
#define PAD_LEFT   0x02
#define PAD_UP     0x04
#define PAD_DOWN   0x08
#define PAD_A      0x10
#define PAD_B      0x20
#define PAD_SELECT 0x40
#define PAD_START  0x80

#define IF_VBLANK 0x01
#define IF_STAT   0x02
#define IF_TIMER  0x04
#define IF_SERIAL 0x08
#define IF_PAD    0x10
#define IF_MASK   0x1F

#define SRAM_BANK_SIZE 0x2000
#define VRAM_BANK_SIZE 0x2000
#define RAM_BANK_SIZE  0x2000
#define ROM_BANK_SIZE  0x4000

enum {
	MBC_NONE = 0,
	MBC_MBC1,
	MBC_MBC2,
	MBC_MBC3,
	MBC_MBC5,
	MBC_MBC6,
	MBC_MBC7,
	MBC_HUC1,
	MBC_HUC3,
	MBC_MMM01,
};

typedef struct
{
	// Metadata
	char name[20];
	un16 checksum;
	un8 cgb, sgb;
	un32 romsize;
	un32 ramsize;
	un32 mbctype;
	bool rumble;
	bool sensor;
	bool batt;
	bool rtc;

	// Onboard memory
	byte *rombanks[512];
	byte *sram;
	un32 sram_dirty;

	// MBC description
	struct {
		int model;
		int rombank;
		int rambank;
		int enableram;
		byte *rmap[0x10];
		byte *wmap[0x10];
	} mbc;

	// Built-in DMG colorization palette (SGB, GBC)
	un16 colorize[4][4];

	FILE *fpRomFile;
} cart_t;

typedef struct
{
	n32 d, h, m, s;
	n32 ticks;
	n32 flags, sel, latch;
	n32 regs[8];
} rtc_t;

typedef struct
{
	byte himem[256];
	byte ibank[8][4096];
	un32 ilines;
	un32 pad;
	un32 cgb;
	n32 hdma;
	n32 serial;
	un8 *bios;
} gameboy_t;

extern gameboy_t hw;
extern cart_t cart;
extern rtc_t rtc;

void rtc_tick();

void hw_reset(bool hard);
void hw_interrupt(byte i, int level);
void hw_pad_refresh(void);
void mem_updatemap(void);
void mem_write(addr_t a, byte b);
byte mem_read(addr_t a);

static inline byte readb(addr_t a)
{
	byte *p = cart.mbc.rmap[a>>12];
	if (p) return p[a];
	return mem_read(a);
}

static inline void writeb(addr_t a, byte b)
{
	byte *p = cart.mbc.wmap[a>>12];
	if (p) p[a] = b;
	else mem_write(a, b);
}

static inline word readw(addr_t a)
{
#ifdef IS_LITTLE_ENDIAN
	if ((a & 0xFFF) == 0xFFF)
#endif
	{
		return readb(a) | (readb(a + 1) << 8);
	}
	byte *p = cart.mbc.rmap[a >> 12];
	if (p)
	{
		return *(word *)(p + a);
	}
	return mem_read(a) | (mem_read(a + 1) << 8);
}

static inline void writew(addr_t a, word w)
{
#ifdef IS_LITTLE_ENDIAN
	if ((a & 0xFFF) == 0xFFF)
#endif
	{
		writeb(a, w);
		writeb(a + 1, w >> 8);
		return;
	}
	byte *p = cart.mbc.wmap[a >> 12];
	if (p)
	{
		*(word *)(p + a) = w;
		return;
	}
	mem_write(a, w);
	mem_write(a + 1, w >> 8);
}
