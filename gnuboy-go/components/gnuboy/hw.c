#include <string.h>
#include <time.h>

#include "gnuboy.h"
#include "cpu.h"
#include "hw.h"
#include "regs.h"
#include "lcd.h"
#include "sound.h"


gameboy_t hw;
cart_t cart;
rtc_t rtc;


/*
 * hw_interrupt changes the virtual interrupt line(s) defined by i
 * The interrupt fires (added to R_IF) when the line transitions from 0 to 1.
 * It does not refire if the line was already high.
 */
void hw_interrupt(byte i, int level)
{
	if (level == 0)
	{
		hw.ilines &= ~i;
	}
	else if ((hw.ilines & i) == 0)
	{
		hw.ilines |= i;
		R_IF |= i; // Fire!

		if ((R_IE & i) != 0)
		{
			// Wake up the CPU when an enabled interrupt occurs
			// IME doesn't matter at this point, only IE
			cpu.halted = 0;
		}
	}
}


/*
 * hw_pad_refresh updates the P1 register from the pad states, generating
 * the appropriate interrupts (by quickly raising and lowering the
 * interrupt line) if a transition has been made.
 */
void hw_pad_refresh()
{
	byte oldp1 = R_P1;
	R_P1 &= 0x30;
	R_P1 |= 0xc0;
	if (!(R_P1 & 0x10)) R_P1 |= (hw.pad & 0x0F);
	if (!(R_P1 & 0x20)) R_P1 |= (hw.pad >> 4);
	R_P1 ^= 0x0F;
	if (oldp1 & ~R_P1 & 0x0F)
	{
		hw_interrupt(IF_PAD, 1);
		hw_interrupt(IF_PAD, 0);
	}
}


/*
 * hw_dma performs plain old memory-to-oam dma, the original dmg
 * dma. Although on the hardware it takes a good deal of time, the cpu
 * continues running during this mode of dma, so no special tricks to
 * stall the cpu are necessary.
 */
static inline void hw_dma(byte b)
{
	addr_t a = ((addr_t)b) << 8;
	for (int i = 0; i < 160; i++, a++)
		lcd.oam.mem[i] = readb(a);
}


static inline void hw_hdma(byte c)
{
	/* Begin or cancel HDMA */
	if ((hw.hdma|c) & 0x80)
	{
		// This transfer will happen in lcd_emulate if needed
		hw.hdma = c;
		R_HDMA5 = c & 0x7f;
		return;
	}

	/* Perform GDMA */
	addr_t sa = ((addr_t)R_HDMA1 << 8) | (R_HDMA2&0xf0);
	addr_t da = 0x8000 | ((addr_t)(R_HDMA3&0x1f) << 8) | (R_HDMA4&0xf0);
	size_t cnt = ((int)c)+1;
	/* FIXME - this should use cpu time! */
	/*cpu_timers(102 * cnt);*/
	cnt <<= 4;
	while (cnt--)
		writeb(da++, readb(sa++));
	R_HDMA1 = sa >> 8;
	R_HDMA2 = sa & 0xF0;
	R_HDMA3 = 0x1F & (da >> 8);
	R_HDMA4 = da & 0xF0;
	R_HDMA5 = 0xFF;
}


/*
 * In order to make reads and writes efficient, we keep tables
 * (indexed by the high nibble of the address) specifying which
 * regions can be read/written without a function call. For such
 * ranges, the pointer in the map table points to the base of the
 * region in host system memory. For ranges that require special
 * processing, the pointer is NULL.
 *
 * This function is also responsible for reading the ROM banks on demand.
 */
void mem_updatemap(void)
{
	cart.mbc.rombank &= (cart.romsize - 1);
	cart.mbc.rambank &= (cart.ramsize - 1);

	int rombank = cart.mbc.rombank;

	if (!cart.rombanks[rombank] && cart.fpRomFile)
	{
		MESSAGE_INFO("loading bank %d.\n", rombank);
		cart.rombanks[rombank] = (byte*)malloc(ROM_BANK_SIZE);
		while (!cart.rombanks[rombank])
		{
			int i = rand() & 0xFF;
			if (cart.rombanks[i])
			{
				MESSAGE_INFO("reclaiming bank %d.\n", i);
				cart.rombanks[rombank] = cart.rombanks[i];
				cart.rombanks[i] = NULL;
			}
		}

		// Load the 16K page
		if (fseek(cart.fpRomFile, rombank * ROM_BANK_SIZE, SEEK_SET)
			|| !fread(cart.rombanks[rombank], ROM_BANK_SIZE, 1, cart.fpRomFile))
		{
			if (feof(cart.fpRomFile))
				MESSAGE_ERROR("End of file reached, the rom's header is probably incorrect!\n");
			else
				gnuboy_panic("ROM bank loading failed");
		}
	}

	// ROM
	cart.mbc.rmap[0x0] = cart.rombanks[0];
	cart.mbc.rmap[0x1] = cart.rombanks[0];
	cart.mbc.rmap[0x2] = cart.rombanks[0];
	cart.mbc.rmap[0x3] = cart.rombanks[0];

	// Force bios to go through mem_read (speed doesn't really matter here)
	if (hw.bios && (R_BIOS & 1) == 0)
	{
		cart.mbc.rmap[0x0] = NULL;
	}

	if (rombank < cart.romsize)
	{
		cart.mbc.rmap[0x4] = cart.rombanks[rombank] - 0x4000;
		cart.mbc.rmap[0x5] = cart.rombanks[rombank] - 0x4000;
		cart.mbc.rmap[0x6] = cart.rombanks[rombank] - 0x4000;
		cart.mbc.rmap[0x7] = cart.rombanks[rombank] - 0x4000;
	}
	else
	{
		cart.mbc.rmap[0x4] = NULL;
		cart.mbc.rmap[0x5] = NULL;
		cart.mbc.rmap[0x6] = NULL;
		cart.mbc.rmap[0x7] = NULL;
	}

	// Video RAM
	cart.mbc.rmap[0x8] = cart.mbc.wmap[0x8] = lcd.vbank[R_VBK & 1] - 0x8000;
	cart.mbc.rmap[0x9] = cart.mbc.wmap[0x9] = lcd.vbank[R_VBK & 1] - 0x8000;

	// Backup RAM
	cart.mbc.rmap[0xA] = NULL;
	cart.mbc.rmap[0xB] = NULL;

	// Work RAM
	cart.mbc.rmap[0xC] = cart.mbc.wmap[0xC] = hw.ibank[0] - 0xC000;

	// ?
	cart.mbc.rmap[0xD] = NULL;

	// IO port and registers
	cart.mbc.rmap[0xF] = cart.mbc.wmap[0xF] = NULL;
}


/* Hardware registers: FF00-FF7F,FFFF */
static inline void ioreg_write(byte r, byte b)
{
	if (!hw.cgb)
	{
		switch (r)
		{
		case RI_BGP:
			pal_write_dmg(0, 0, b);
			pal_write_dmg(8, 1, b);
			break;
		case RI_OBP0:
			pal_write_dmg(64, 2, b);
			break;
		case RI_OBP1:
			pal_write_dmg(72, 3, b);
			break;

		// These don't exist on DMG:
		case RI_VBK:
		case RI_BCPS:
		case RI_OCPS:
		case RI_BCPD:
		case RI_OCPD:
		case RI_SVBK:
		case RI_KEY1:
		case RI_HDMA1:
		case RI_HDMA2:
		case RI_HDMA3:
		case RI_HDMA4:
		case RI_HDMA5:
			return;
		}
	}

	switch(r)
	{
	case RI_TIMA:
	case RI_TMA:
	case RI_TAC:
	case RI_SCY:
	case RI_SCX:
	case RI_WY:
	case RI_WX:
	case RI_BGP:
	case RI_OBP0:
	case RI_OBP1:
		REG(r) = b;
		break;
	case RI_IF:
	case RI_IE:
		REG(r) = b & 0x1F;
		break;
	case RI_P1:
		REG(r) = b;
		hw_pad_refresh();
		break;
	case RI_SC:
		if ((b & 0x81) == 0x81)
			hw.serial = 1952; // 8 * 122us;
		else
			hw.serial = 0;
		R_SC = b; /* & 0x7f; */
		break;
	case RI_SB:
		REG(r) = b;
		break;
	case RI_DIV:
		REG(r) = 0;
		break;
	case RI_LCDC:
		lcdc_change(b);
		break;
	case RI_STAT:
		R_STAT = (R_STAT & 0x07) | (b & 0x78);
		if (!hw.cgb && !(R_STAT & 2)) /* DMG STAT write bug => interrupt */
			hw_interrupt(IF_STAT, 1);
		stat_trigger();
		break;
	case RI_LYC:
		REG(r) = b;
		stat_trigger();
		break;
	case RI_VBK:
		REG(r) = b | 0xFE;
		mem_updatemap();
		break;
	case RI_BCPS:
		R_BCPS = b & 0xBF;
		R_BCPD = lcd.pal[b & 0x3F];
		break;
	case RI_OCPS:
		R_OCPS = b & 0xBF;
		R_OCPD = lcd.pal[64 + (b & 0x3F)];
		break;
	case RI_BCPD:
		R_BCPD = b;
		pal_write(R_BCPS & 0x3F, b);
		if (R_BCPS & 0x80) R_BCPS = (R_BCPS+1) & 0xBF;
		break;
	case RI_OCPD:
		R_OCPD = b;
		pal_write(64 + (R_OCPS & 0x3F), b);
		if (R_OCPS & 0x80) R_OCPS = (R_OCPS+1) & 0xBF;
		break;
	case RI_SVBK:
		REG(r) = b | 0xF8;
		mem_updatemap();
		break;
	case RI_DMA:
		hw_dma(b);
		break;
	case RI_KEY1:
		REG(r) = (REG(r) & 0x80) | (b & 0x01);
		break;
	case RI_BIOS:
		REG(r) = b;
		mem_updatemap();
		break;
	case RI_HDMA1:
	case RI_HDMA2:
	case RI_HDMA3:
	case RI_HDMA4:
		REG(r) = b;
		break;
	case RI_HDMA5:
		hw_hdma(b);
		break;
	default:
		if (r >= 0x10 && r < 0x40) {
			sound_write(r, b);
		}
	}

	MESSAGE_DEBUG("reg %02X => %02X (%02X)\n", r, REG(r), b);
}


/* Hardware registers: FF00-FF7F,FFFF */
static inline byte ioreg_read(byte r)
{
	switch(r)
	{
	case RI_SB:
	case RI_SC:
	case RI_P1:
	case RI_DIV:
	case RI_TIMA:
	case RI_TMA:
	case RI_TAC:
	case RI_LCDC:
	case RI_STAT:
	case RI_SCY:
	case RI_SCX:
	case RI_LY:
	case RI_LYC:
	case RI_BGP:
	case RI_OBP0:
	case RI_OBP1:
	case RI_WY:
	case RI_WX:
	case RI_IE:
	case RI_IF:
	//	return REG(r);
	// CGB-specific registers
	case RI_VBK:
	case RI_BCPS:
	case RI_OCPS:
	case RI_BCPD:
	case RI_OCPD:
	case RI_SVBK:
	case RI_KEY1:
	case RI_BIOS:
	case RI_HDMA1:
	case RI_HDMA2:
	case RI_HDMA3:
	case RI_HDMA4:
	case RI_HDMA5:
		// if (hw.cgb) return REG(r);
		return REG(r);
	default:
		if (r >= 0x10 && r < 0x40) {
			return sound_read(r);
		}
		return 0xFF;
	}
}


static void rtc_write(byte b)
{
	MESSAGE_DEBUG("write %02X: %02X (%d)\n", rtc.sel, b, b);

	switch (rtc.sel & 0xf)
	{
	case 0x8: // Seconds
		rtc.regs[0] = b;
		rtc.s = b % 60;
		break;
	case 0x9: // Minutes
		rtc.regs[1] = b;
		rtc.m = b % 60;
		break;
	case 0xA: // Hours
		rtc.regs[2] = b;
		rtc.h = b % 24;
		break;
	case 0xB: // Days (lower 8 bits)
		rtc.regs[3] = b;
		rtc.d = ((rtc.d & 0x100) | b) % 365;
		break;
	case 0xC: // Flags (days upper 1 bit, carry, stop)
		rtc.regs[4] = b;
		rtc.flags = b;
		rtc.d = ((rtc.d & 0xff) | ((b&1)<<9)) % 365;
		break;
	}
}


void rtc_tick()
{
	if ((rtc.flags & 0x40))
		return; // rtc stop

	if (++rtc.ticks >= 60)
	{
		if (++rtc.s >= 60)
		{
			if (++rtc.m >= 60)
			{
				if (++rtc.h >= 24)
				{
					if (++rtc.d >= 365)
					{
						rtc.d = 0;
						rtc.flags |= 0x80;
					}
					rtc.h = 0;
				}
				rtc.m = 0;
			}
			rtc.s = 0;
		}
		rtc.ticks = 0;
	}
}


/*
 * Memory bank controllers typically intercept write attempts to
 * 0000-7FFF, using the address and byte written as instructions to
 * change rom or sram banks, control special hardware, etc.
 *
 * mbc_write takes an address (which should be in the proper range)
 * and a byte value written to the address.
 */
static inline void mbc_write(addr_t a, byte b)
{
	MESSAGE_DEBUG("cart.mbc %d: rom bank %02X -[%04X:%02X]-> ", cart.mbctype, cart.mbc.rombank, a, b);

	switch (cart.mbctype)
	{
	case MBC_MBC1:
		switch (a & 0xE000)
		{
		case 0x0000:
			cart.mbc.enableram = ((b & 0x0F) == 0x0A);
			break;
		case 0x2000:
			if ((b & 0x1F) == 0) b = 0x01;
			cart.mbc.rombank = (cart.mbc.rombank & 0x60) | (b & 0x1F);
			break;
		case 0x4000:
			if (cart.mbc.model)
			{
				cart.mbc.rambank = b & 0x03;
				break;
			}
			cart.mbc.rombank = (cart.mbc.rombank & 0x1F) | ((int)(b&3)<<5);
			break;
		case 0x6000:
			cart.mbc.model = b & 0x1;
			break;
		}
		break;

	case MBC_MBC2: /* is this at all right? */
		if ((a & 0x0100) == 0x0000)
		{
			cart.mbc.enableram = ((b & 0x0F) == 0x0A);
			break;
		}
		if ((a & 0xE100) == 0x2100)
		{
			cart.mbc.rombank = b & 0x0F;
			break;
		}
		break;

	case MBC_MBC3:
		switch (a & 0xE000)
		{
		case 0x0000:
			cart.mbc.enableram = ((b & 0x0F) == 0x0A);
			break;
		case 0x2000:
			b &= 0x7F;
			cart.mbc.rombank = b ? b : 1;
			break;
		case 0x4000:
			rtc.sel = b & 0x0f;
			cart.mbc.rambank = b & 0x03;
			break;
		case 0x6000:
			if ((rtc.latch ^ b) & b & 1)
			{
				rtc.regs[0] = rtc.s;
				rtc.regs[1] = rtc.m;
				rtc.regs[2] = rtc.h;
				rtc.regs[3] = rtc.d;
				rtc.regs[4] = rtc.flags;
				rtc.regs[5] = 0xff;
				rtc.regs[6] = 0xff;
				rtc.regs[7] = 0xff;
			}
			rtc.latch = b;
			break;
		}
		break;

	case MBC_MBC5:
		switch (a & 0x7000)
		{
		case 0x0000:
		case 0x1000:
			cart.mbc.enableram = ((b & 0x0F) == 0x0A);
			break;
		case 0x2000:
			cart.mbc.rombank = (cart.mbc.rombank & 0x100) | (b);
			break;
		case 0x3000:
			cart.mbc.rombank = (cart.mbc.rombank & 0xFF) | ((int)(b & 1) << 8);
			break;
		case 0x4000:
		case 0x5000:
			if (cart.rumble)
				cart.mbc.rambank = b & 0x0F;
			else
				cart.mbc.rambank = b & ~8;
			break;
		case 0x6000:
		case 0x7000:
			// Nothing but Radikal Bikers tries to access it.
			break;
		default:
			MESSAGE_ERROR("MBC_MBC5: invalid write to 0x%x (0x%x)\n", a, b);
			break;
		}
		break;

	case MBC_HUC1: /* FIXME - this is all guesswork -- is it right??? */
	case MBC_HUC3: // not implemented (the previous code was wrong, just a copy of MBC3...)
		switch (a & 0xE000)
		{
		case 0x0000:
			cart.mbc.enableram = ((b & 0x0F) == 0x0A);
			break;
		case 0x2000:
			if ((b & 0x1F) == 0) b = 0x01;
			cart.mbc.rombank = (cart.mbc.rombank & 0x60) | (b & 0x1F);
			break;
		case 0x4000:
			if (cart.mbc.model)
			{
				cart.mbc.rambank = b & 0x03;
				break;
			}
			cart.mbc.rombank = (cart.mbc.rombank & 0x1F) | ((int)(b&3)<<5);
			break;
		case 0x6000:
			cart.mbc.model = b & 0x1;
			break;
		}
		break;
	}

	MESSAGE_DEBUG("%02X\n", cart.mbc.rombank);

	mem_updatemap();
}


/*
 * mem_write is the main bus write function. For speed reasons, writeb() should
 * be called because it handles RAM directly first, then falls back to mem_write.
 */
void mem_write(addr_t a, byte b)
{
	MESSAGE_DEBUG("write to 0x%04X: 0x%02X\n", a, b);

	switch (a & 0xE000)
	{
	case 0x0000:
	case 0x2000:
	case 0x4000:
	case 0x6000:
		mbc_write(a, b);
		break;

	case 0x8000:
		lcd.vbank[R_VBK&1][a & 0x1FFF] = b;
		break;

	case 0xA000:
		if (!cart.mbc.enableram) break;
		if (rtc.sel & 8)
		{
			rtc_write(b);
		}
		else
		{
			int offset = (cart.mbc.rambank << 13) | (a & 0x1FFF);
			if (cart.sram[offset] != b)
			{
				cart.sram[offset] = b;
				cart.sram_dirty |= (1 << cart.mbc.rambank);
			}
		}
		break;

	case 0xC000:
		if ((a & 0xF000) == 0xC000)
			hw.ibank[0][a & 0x0FFF] = b;
		else
			hw.ibank[(R_SVBK & 0x7) ?: 1][a & 0x0FFF] = b;
		break;

	case 0xE000:
		if (a < 0xFE00)
		{
			writeb(a & 0xDFFF, b);
		}
		else if ((a & 0xFF00) == 0xFE00)
		{
			if (a < 0xFEA0) lcd.oam.mem[a & 0xFF] = b;
		}
		else if (a >= 0xFF10 && a <= 0xFF3F)
		{
			sound_write(a & 0xFF, b);
		}
		else if ((a & 0xFF80) == 0xFF80 && a != 0xFFFF)
		{
			hw.himem[a & 0xFF] = b;
		}
		else
		{
			ioreg_write(a & 0xFF, b);
		}
	}
}


/*
 * mem_read is the main bus read function. For speed reasons, readb() should
 * be called because it handles RAM directly first, then falls back to mem_read.
 */
byte mem_read(addr_t a)
{
	MESSAGE_DEBUG("read %04x\n", a);

	switch (a & 0xE000)
	{
	case 0x0000:
		if (a < 0x900 && (R_BIOS & 1) == 0)
		{
			if (a < 0x100)
				return hw.bios[a];
			if (hw.cgb && a >= 0x200)
				return hw.bios[a];
		}
		// fall through
	case 0x2000:
		return cart.rombanks[0][a & 0x3fff];

	case 0x4000:
	case 0x6000:
		return cart.rombanks[cart.mbc.rombank][a & 0x3FFF];

	case 0x8000:
		return lcd.vbank[R_VBK&1][a & 0x1FFF];

	case 0xA000:
		if (!cart.mbc.enableram && cart.mbctype == MBC_HUC3)
			return 0x01;
		else if (!cart.mbc.enableram)
			return 0xFF;
		else if (rtc.sel & 8)
			return rtc.regs[rtc.sel & 7];
		else
			return cart.sram[(cart.mbc.rambank << 13) | (a & 0x1FFF)];

	case 0xC000:
		if ((a & 0xF000) == 0xC000)
			return hw.ibank[0][a & 0x0FFF];
		return hw.ibank[(R_SVBK & 0x7) ?: 1][a & 0x0FFF];

	case 0xE000:
		if (a < 0xFE00)
		{
			return readb(a & 0xDFFF);
		}
		else if ((a & 0xFF00) == 0xFE00)
		{
			return (a < 0xFEA0) ? lcd.oam.mem[a & 0xFF] : 0xFF;
		}
		else if (a >= 0xFF10 && a <= 0xFF3F)
		{
			return sound_read(a & 0xFF);
		}
		else
		// else if ((a & 0xFF80) == 0xFF80)
		{
			return REG(a & 0xFF);
		}

		// return ioreg_read(a & 0xFF);
	}
	return 0xFF;
}


void hw_reset(bool hard)
{
	hw.ilines = 0;
	hw.serial = 0;
	hw.hdma = 0;
	hw.pad = 0;

	memset(hw.himem, 0, sizeof(hw.himem));
	R_P1 = 0xFF;
	R_LCDC = 0x91;
	R_BGP = 0xFC;
	R_OBP0 = 0xFF;
	R_OBP1 = 0xFF;
	R_SVBK = 0xF9;
	R_HDMA5 = 0xFF;
	R_VBK = 0xFE;

	if (hard)
	{
		memset(hw.ibank, 0xff, 4096 * 8);
		memset(cart.sram, 0xff, 8192 * cart.ramsize);
		memset(rtc.regs, 0x00, sizeof(rtc.regs));
	}

	memset(&cart.mbc, 0, sizeof(cart.mbc));
	cart.mbc.rombank = 1;
	cart.sram_dirty = 0;

	rtc.ticks = 0;
	rtc.flags = 0;
	rtc.sel = 0;
	rtc.latch = 0;

	mem_updatemap();
}
