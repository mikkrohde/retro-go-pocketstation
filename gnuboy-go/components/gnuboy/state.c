#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include "gnuboy.h"
#include "regs.h"
#include "hw.h"
#include "lcd.h"
#include "cpu.h"
#include "sound.h"

#ifdef IS_LITTLE_ENDIAN
#define LIL(x) (x)
#else
#define LIL(x) ((x<<24)|((x&0xff00)<<8)|((x>>8)&0xff00)|(x>>24))
#endif

#define SAVE_VERSION 0x107

// Set in the far future for VBA-M support
#define RT_BASE 1893456000

#define wavofs  (4096 - 784)
#define hiofs   (4096 - 768)
#define palofs  (4096 - 512)
#define oamofs  (4096 - 256)

#define I1(s, p) { 1, s, p }
#define I2(s, p) { 2, s, p }
#define I4(s, p) { 4, s, p }
#define END { 0, "\0\0\0\0", 0 }

typedef struct
{
	int len;
	char key[4];
	void *ptr;
} svar_t;

typedef struct
{
	void *ptr;
	int len;
} sblock_t;

static un32 ver;

static const svar_t svars[] =
{
	I4("GbSs", &ver),

	I2("PC  ", &PC),
	I2("SP  ", &SP),
	I2("BC  ", &BC),
	I2("DE  ", &DE),
	I2("HL  ", &HL),
	I2("AF  ", &AF),

	I4("IME ", &cpu.ime),
	I4("ima ", &cpu.ima),
	I4("spd ", &cpu.double_speed),
	I4("halt", &cpu.halted),
	I4("div ", &cpu.div),
	I4("tim ", &cpu.timer),
	I4("lcdc", &lcd.cycles),
	I4("snd ", &snd.cycles),

	I4("ints", &hw.ilines),
	I4("pad ", &hw.pad),
	I4("cgb ", &hw.cgb),
	I4("hdma", &hw.hdma),
	I4("seri", &hw.serial),

	I4("mbcm", &cart.mbc.model),
	I4("romb", &cart.mbc.rombank),
	I4("ramb", &cart.mbc.rambank),
	I4("enab", &cart.mbc.enableram),
	// I4("batt", &cart.batt),

	I4("rtcR", &rtc.sel),
	I4("rtcL", &rtc.latch),
	I4("rtcF", &rtc.flags),
	I4("rtcd", &rtc.d),
	I4("rtch", &rtc.h),
	I4("rtcm", &rtc.m),
	I4("rtcs", &rtc.s),
	I4("rtct", &rtc.ticks),
	I1("rtR8", &rtc.regs[0]),
	I1("rtR9", &rtc.regs[1]),
	I1("rtRA", &rtc.regs[2]),
	I1("rtRB", &rtc.regs[3]),
	I1("rtRC", &rtc.regs[4]),

	I4("S1on", &snd.ch[0].on),
	I4("S1p ", &snd.ch[0].pos),
	I4("S1c ", &snd.ch[0].cnt),
	I4("S1ec", &snd.ch[0].encnt),
	I4("S1sc", &snd.ch[0].swcnt),
	I4("S1sf", &snd.ch[0].swfreq),

	I4("S2on", &snd.ch[1].on),
	I4("S2p ", &snd.ch[1].pos),
	I4("S2c ", &snd.ch[1].cnt),
	I4("S2ec", &snd.ch[1].encnt),

	I4("S3on", &snd.ch[2].on),
	I4("S3p ", &snd.ch[2].pos),
	I4("S3c ", &snd.ch[2].cnt),

	I4("S4on", &snd.ch[3].on),
	I4("S4p ", &snd.ch[3].pos),
	I4("S4c ", &snd.ch[3].cnt),
	I4("S4ec", &snd.ch[3].encnt),

	END
};


static void rtc_save(FILE *f)
{
	int64_t rt = RT_BASE + (rtc.s + (rtc.m * 60) + (rtc.h * 3600) + (rtc.d * 86400));

	fwrite(&rtc.s, 4, 1, f);
	fwrite(&rtc.m, 4, 1, f);
	fwrite(&rtc.h, 4, 1, f);
	fwrite(&rtc.d, 4, 1, f);
	fwrite(&rtc.flags, 4, 1, f);
	for (int i = 0; i < 5; i++) {
		fwrite(&rtc.regs[i], 4, 1, f);
	}
	fwrite(&rt, 8, 1, f);
}


static void rtc_load(FILE *f)
{
	int64_t rt = 0;

	// Try to read old format first
	int tmp = fscanf(f, "%d %*d %d %02d %02d %02d %02d\n%*d\n",
		&rtc.flags, &rtc.d, &rtc.h, &rtc.m, &rtc.s, &rtc.ticks);

	if (tmp >= 5)
		return;

	fread(&rtc.s, 4, 1, f);
	fread(&rtc.m, 4, 1, f);
	fread(&rtc.h, 4, 1, f);
	fread(&rtc.d, 4, 1, f);
	fread(&rtc.flags, 4, 1, f);
	for (int i = 0; i < 5; i++) {
		fread(&rtc.regs[i], 4, 1, f);
	}
	fread(&rt, 8, 1, f);
}


int gnuboy_load_sram(const char *file)
{
	int ret = -1;
	FILE *f;

	if (!cart.batt || !cart.ramsize || !file || !*file)
		return -1;

	if ((f = fopen(file, "rb")))
	{
		MESSAGE_INFO("Loading SRAM from '%s'\n", file);
		if (fread(cart.sram, 8192, cart.ramsize, f))
		{
			cart.sram_dirty = 0;
			rtc_load(f);
			ret = 0;
		}
		fclose(f);
	}

	return ret;
}


int gnuboy_save_sram(const char *file)
{
	int ret = -1;
	FILE *f;

	if (!cart.batt || !cart.ramsize || !file || !*file)
		return -1;

	if ((f = fopen(file, "wb")))
	{
		MESSAGE_INFO("Saving SRAM to '%s'\n", file);
		if (fwrite(cart.sram, 8192, cart.ramsize, f))
		{
			cart.sram_dirty = 0;
			rtc_save(f);
			ret = 0;
		}
		fclose(f);
	}

	return ret;
}


int gnuboy_update_sram(const char *file)
{
	if (!cart.batt || !cart.ramsize || !file || !*file)
		return -1;

	FILE *fp = fopen(file, "wb");
	if (!fp)
	{
		MESSAGE_ERROR("Unable to open SRAM file: %s", file);
		goto _cleanup;
	}

	for (int bank = 0; bank < cart.ramsize; ++bank)
	{
		if (cart.sram_dirty & (1 << bank))
		{
			if (fseek(fp, bank * 8192, SEEK_SET) != 0)
			{
				MESSAGE_ERROR("Failed to seek sram bank #%d\n", bank);
				// MESSAGE_ERROR("Failed to seek sram sector #%d\n", sector);
				goto _cleanup;
			}

			if (fwrite(cart.sram + (bank * 8192), 8192, 1, fp) != 1)
			{
				MESSAGE_ERROR("Failed to write sram bank #%d\n", bank);
				// MESSAGE_ERROR("Failed to write sram sector #%d\n", sector);
				goto _cleanup;
			}

			cart.sram_dirty &= ~(1 << bank);
		}
	}

	if (fseek(fp, cart.ramsize * 8192, SEEK_SET) == 0)
	{
		rtc_save(fp);
	}

_cleanup:

	if (fclose(fp) != 0)
		return -2;

	// Still dirty is an error
	if (cart.sram_dirty)
		return -3;

	return 0;
}


/**
 * Save file format is:
 * GB:
 * 0x0000 - 0x0FFF: svars, oam, palette, hiram, wave
 * 0x1000 - 0x2FFF: RAM
 * 0x3000 - 0x4FFF: VRAM
 * 0x5000 - 0x...:  SRAM
 *
 * GBC:
 * 0x0000 - 0x0FFF: svars, oam, palette, hiram, wave
 * 0x1000 - 0x8FFF: RAM
 * 0x9000 - 0xCFFF: VRAM
 * 0xD000 - 0x...:  SRAM
 *
 */

int gnuboy_save_state(const char *file)
{
	byte *buf = calloc(1, 4096);
	if (!buf) return -2;

	FILE *fp = fopen(file, "wb");
	if (!fp) goto _error;

	sblock_t blocks[] = {
		{buf, 1},
		{hw.ibank, hw.cgb ? 8 : 2},
		{lcd.vbank, hw.cgb ? 4 : 2},
		{cart.sram, cart.ramsize * 2},
		{NULL, 0},
	};

	un32 (*header)[2] = (un32 (*)[2])buf;

	ver = SAVE_VERSION;

	for (int i = 0; svars[i].ptr; i++)
	{
		un32 d = 0;

		switch (svars[i].len)
		{
		case 1:
			d = *(byte *)svars[i].ptr;
			break;
		case 2:
			d = *(un16 *)svars[i].ptr;
			break;
		case 4:
			d = *(un32 *)svars[i].ptr;
			break;
		}

		header[i][0] = *(un32 *)svars[i].key;
		header[i][1] = LIL(d);
	}

	memcpy(buf+hiofs, hw.himem, sizeof hw.himem);
	memcpy(buf+palofs, lcd.pal, sizeof lcd.pal);
	memcpy(buf+oamofs, lcd.oam.mem, sizeof lcd.oam);
	memcpy(buf+wavofs, snd.wave, sizeof snd.wave);

	for (int i = 0; blocks[i].ptr != NULL; i++)
	{
		if (fwrite(blocks[i].ptr, 4096, blocks[i].len, fp) < 1)
		{
			MESSAGE_ERROR("Write error in block %d\n", i);
			goto _error;
		}
	}

	fclose(fp);
	free(buf);

	return 0;

_error:
	if (fp) fclose(fp);
	if (buf) free(buf);

	return -1;
}


int gnuboy_load_state(const char *file)
{
	byte* buf = calloc(1, 4096);
	if (!buf) return -2;

	FILE *fp = fopen(file, "rb");
	if (!fp) goto _error;

	sblock_t blocks[] = {
		{buf, 1},
		{hw.ibank, hw.cgb ? 8 : 2},
		{lcd.vbank, hw.cgb ? 4 : 2},
		{cart.sram, cart.ramsize * 2},
		{NULL, 0},
	};

	for (int i = 0; blocks[i].ptr != NULL; i++)
	{
		if (fread(blocks[i].ptr, 4096, blocks[i].len, fp) < 1)
		{
			MESSAGE_ERROR("Read error in block %d\n", i);
			goto _error;
		}
	}

	un32 (*header)[2] = (un32 (*)[2])buf;

	for (int i = 0; svars[i].ptr; i++)
	{
		un32 d = 0;

		for (int j = 0; header[j][0]; j++)
		{
			if (header[j][0] == *(un32 *)svars[i].key)
			{
				d = LIL(header[j][1]);
				break;
			}
		}

		switch (svars[i].len)
		{
		case 1:
			*(byte *)svars[i].ptr = d;
			break;
		case 2:
			*(un16 *)svars[i].ptr = d;
			break;
		case 4:
			*(un32 *)svars[i].ptr = d;
			break;
		}
	}

	if (ver != SAVE_VERSION)
		MESSAGE_ERROR("Save file version mismatch!\n");

	memcpy(hw.himem, buf+hiofs, sizeof hw.himem);
	memcpy(lcd.pal, buf+palofs, sizeof lcd.pal);
	memcpy(lcd.oam.mem, buf+oamofs, sizeof lcd.oam);
	memcpy(snd.wave, buf+wavofs, sizeof snd.wave);

	fclose(fp);
	free(buf);

	// Disable BIOS. This is a hack to support old saves
	R_BIOS = 0x1;

	pal_dirty();
	sound_dirty();
	mem_updatemap();

	return 0;

_error:
	if (fp) fclose(fp);
	if (buf) free(buf);

	return -1;
}
