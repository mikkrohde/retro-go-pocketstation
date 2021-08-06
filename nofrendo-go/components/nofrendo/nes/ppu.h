/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nes/ppu.h: Graphics emulation header
**
*/

#ifndef _NES_PPU_H_
#define _NES_PPU_H_

/* PPU register defines */
#define  PPU_CTRL0            0x2000
#define  PPU_CTRL1            0x2001
#define  PPU_STAT             0x2002
#define  PPU_OAMADDR          0x2003
#define  PPU_OAMDATA          0x2004
#define  PPU_SCROLL           0x2005
#define  PPU_VADDR            0x2006
#define  PPU_VDATA            0x2007

#define  PPU_OAMDMA           0x4014

/* $2000 */
#define  PPU_CTRL0F_NMI       0x80
#define  PPU_CTRL0F_OBJ16     0x20
#define  PPU_CTRL0F_BGADDR    0x10
#define  PPU_CTRL0F_OBJADDR   0x08
#define  PPU_CTRL0F_ADDRINC   0x04
#define  PPU_CTRL0F_NAMETAB   0x03

/* $2001 */
#define  PPU_CTRL1F_OBJON     0x10
#define  PPU_CTRL1F_BGON      0x08
#define  PPU_CTRL1F_OBJMASK   0x04
#define  PPU_CTRL1F_BGMASK    0x02

/* $2002 */
#define  PPU_STATF_VBLANK     0x80
#define  PPU_STATF_STRIKE     0x40
#define  PPU_STATF_MAXSPRITE  0x20

/* Sprite attribute byte bitmasks */
#define  OAMF_VFLIP           0x80
#define  OAMF_HFLIP           0x40
#define  OAMF_BEHIND          0x20

/* Maximum number of sprites per horizontal scanline */
#define  PPU_MAXSPRITE        8

/* Predefined input palette count */
#define  PPU_PAL_COUNT        6

/* Some mappers need to hook into the PPU's internals */
typedef void (*ppu_latchfunc_t)(uint32_t address, uint8_t value);
typedef uint8_t (*ppu_vreadfunc_t)(uint32_t address, uint8_t value);

typedef enum
{
   PPU_DRAW_SPRITES,
   PPU_DRAW_BACKGROUND,
   PPU_LIMIT_SPRITES,
   PPU_PALETTE_RGB,
   PPU_OPTIONS_COUNT,
} ppu_option_t;

typedef enum
{
   PPU_MIRROR_SCR0 = 0,
   PPU_MIRROR_SCR1 = 1,
   PPU_MIRROR_HORI = 2,
   PPU_MIRROR_VERT = 3,
   PPU_MIRROR_FOUR = 4,
} ppu_mirror_t;

typedef struct
{
    uint8_t r, g, b;
} ppu_rgb_t;

typedef struct
{
   uint8_t y_loc;
   uint8_t tile;
   uint8_t attr;
   uint8_t x_loc;
} ppu_obj_t;

typedef struct
{
   /* The NES has only 2 nametables, but we allocate 4 for mappers to use */
   uint8_t nametab[0x400 * 4];

   /* Sprite memory */
   uint8_t oam[256];

   /* Internal palette */
   uint8_t palette[32];

   /* VRAM (CHR RAM/ROM) paging */
   uint8_t *page[16];

   /* Framebuffer palette */
   ppu_rgb_t curpal[256];
   bool curpal_dirty;

   /* Hardware registers */
   uint8_t ctrl0, ctrl1, stat, oam_addr, nametab_base;
   uint8_t latch, vdata_latch, tile_xofs, flipflop;
   uint  vaddr, vaddr_latch, vaddr_inc;
   uint nt1, nt2, nt3, nt4;

   uint obj_height, obj_base, bg_base;
   bool left_bg_on, left_obj_on;
   bool bg_on, obj_on;

   bool strikeflag;
   uint strike_cycle;

   int scanline;
   int last_scanline;

   /* Determines if left column can be cropped/blanked */
   int left_bg_counter;

   /* Callbacks for naughty mappers */
   ppu_latchfunc_t latchfunc;
   ppu_vreadfunc_t vreadfunc;

   bool vram_accessible;
   bool vram_present;

   /* Misc runtime options */
   int options[16];
} ppu_t;

typedef struct
{
   char  name[16];
   uint8_t data[192];
} palette_t;

/* Mirroring / Paging */
extern void ppu_setpage(int size, int page_num, uint8_t *location);
extern void ppu_setnametables(int nt1, int nt2, int nt3, int nt4);
extern void ppu_setmirroring(ppu_mirror_t type);
extern uint8_t *ppu_getpage(int page_num);
extern uint8_t *ppu_getnametable(int nt);

/* Control */
extern ppu_t *ppu_init(void);
extern void ppu_refresh(void);
extern void ppu_reset(void);
extern void ppu_shutdown(void);
extern bool ppu_enabled(void);
extern bool ppu_inframe(void);
extern void ppu_setopt(ppu_option_t n, int val);
extern int  ppu_getopt(ppu_option_t n);

extern void ppu_setlatchfunc(ppu_latchfunc_t func);
extern void ppu_setvreadfunc(ppu_vreadfunc_t func);

extern void ppu_getcontext(ppu_t *dest_ppu);
extern void ppu_setcontext(ppu_t *src_ppu);

/* IO */
extern uint8_t ppu_read(uint32_t address);
extern void ppu_write(uint32_t address, uint8_t value);

/* Rendering */
extern void ppu_scanline(uint8_t *bmp, int scanline, bool draw_flag);
extern void ppu_endscanline(void);
extern void ppu_setpalette(ppu_rgb_t *pal);
extern const palette_t *ppu_getpalette(int n);

/* Debugging */
extern void ppu_dumppattern(uint8_t *bmp, int table_num, int x_loc, int y_loc, int col);
extern void ppu_dumpoam(uint8_t *bmp, int x_loc, int y_loc);

/* PPU debug drawing */
#define  GUI_FIRSTENTRY 192

enum
{
   GUI_BLACK = GUI_FIRSTENTRY,
   GUI_DKGRAY,
   GUI_GRAY,
   GUI_LTGRAY,
   GUI_WHITE,
   GUI_RED,
   GUI_GREEN,
   GUI_BLUE,
   GUI_LASTENTRY
};

#define  GUI_TOTALCOLORS   (GUI_LASTENTRY - GUI_FIRSTENTRY)

#endif /* _NES_PPU_H_ */
