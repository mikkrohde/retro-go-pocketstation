#include <stdio.h>
#include <time.h>

#include "gnuboy.h"
#include "regs.h"
#include "hw.h"
#include "cpu.h"
#include "sound.h"
#include "lcd.h"
#include "palettes.h"

static const char *mbc_names[16] = {
	"MBC_NONE", "MBC_MBC1", "MBC_MBC2", "MBC_MBC3",
	"MBC_MBC5", "MBC_MBC6", "MBC_MBC7", "MBC_HUC1",
	"MBC_HUC3", "MBC_MMM01", "INVALID", "INVALID",
	"INVALID", "INVALID", "INVALID", "INVALID",
};


bool gnuboy_init(int sample_rate, bool stereo)
{
	if (sound_init(sample_rate, stereo) < 0)
		return false;

	gnuboy_reset(0);
	return true;
}


void gnuboy_deinit(void)
{
	//
}


void gnuboy_reset(bool hard)
{
	hw_reset(hard);
	lcd_reset(hard);
	cpu_reset(hard);
	sound_reset(hard);
}


/*
	Time intervals throughout the code, unless otherwise noted, are
	specified in double-speed machine cycles (2MHz), each unit
	roughly corresponds to 0.477us.

	For CPU each cycle takes 2dsc (0.954us) in single-speed mode
	and 1dsc (0.477us) in double speed mode.

	Although hardware gbc LCDC would operate at completely different
	and fixed frequency, for emulation purposes timings for it are
	also specified in double-speed cycles.

	line = 228 dsc (109us)
	frame (154 lines) = 35112 dsc (16.7ms)
	of which
		visible lines x144 = 32832 dsc (15.66ms)
		vblank lines x10 = 2280 dsc (1.08ms)
*/
void gnuboy_run(bool draw)
{
	fb.enabled = draw;
	pcm.pos = 0;

	/* FIXME: judging by the time specified this was intended
	to emulate through vblank phase which is handled at the
	end of the loop. */
	cpu_emulate(2280);

	/* FIXME: R_LY >= 0; comparsion to zero can also be removed
	altogether, R_LY is always 0 at this point */
	while (R_LY > 0 && R_LY < 144) {
		/* Step through visible line scanning phase */
		cpu_emulate(lcd.cycles);
	}

	/* VBLANK BEGIN */
	if (draw && fb.blit_func) {
		(fb.blit_func)();
	}

	// sys_vsync();

	rtc_tick();

	sound_mix();

	if (!(R_LCDC & 0x80)) {
		/* LCDC operation stopped */
		/* FIXME: judging by the time specified, this is
		intended to emulate through visible line scanning
		phase, even though we are already at vblank here */
		cpu_emulate(32832);
	}

	while (R_LY > 0) {
		/* Step through vblank phase */
		cpu_emulate(lcd.cycles);
	}
}

void gnuboy_panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	abort();
	// RG_PANIC(fmt); // Lazy
}


int gnuboy_load_rom(const char *file, bool preload)
{
	MESSAGE_INFO("Loading file: '%s'\n", file);

	cart.fpRomFile = fopen(file, "rb");
	byte header[0x400];

	if (!cart.fpRomFile || fread(header, sizeof(header), 1, cart.fpRomFile) < 1)
	{
		return -1;
	}

	fseek(cart.fpRomFile, 0, SEEK_SET);

	int type = header[0x0147];
	int romsize = header[0x0148];
	int ramsize = header[0x0149];

	memcpy(&cart.checksum, header + 0x014E, 2);
	memcpy(&cart.name, header + 0x0134, 16);
	cart.name[16] = 0;
	cart.cgb = header[0x0143] >> 6;
	cart.sgb = header[0x0146] & 3;

	cart.batt = (type == 3 || type == 6 || type == 9 || type == 13 || type == 15 ||
				type == 16 || type == 19 || type == 27 || type == 30 || type == 255);
	cart.rtc  = (type == 15 || type == 16);
	cart.rumble = (type == 28 || type == 29 || type == 30);
	cart.sensor = (type == 34);

	if (type >= 1 && type <= 3)
		cart.mbctype = MBC_MBC1;
	else if (type >= 5 && type <= 6)
		cart.mbctype = MBC_MBC2;
	else if (type >= 11 && type <= 13)
		cart.mbctype = MBC_MMM01;
	else if (type >= 15 && type <= 19)
		cart.mbctype = MBC_MBC3;
	else if (type >= 25 && type <= 30)
		cart.mbctype = MBC_MBC5;
	else if (type == 32)
		cart.mbctype = MBC_MBC6;
	else if (type == 34)
		cart.mbctype = MBC_MBC7;
	else if (type == 254)
		cart.mbctype = MBC_HUC3;
	else if (type == 255)
		cart.mbctype = MBC_HUC1;
	else
		cart.mbctype = MBC_NONE;

	if (romsize < 9)
	{
		cart.romsize = (2 << romsize);
	}
	else if (romsize > 0x51 && romsize < 0x55)
	{
		cart.romsize = 128; // (2 << romsize) + 64;
	}
	else
	{
		gnuboy_panic("Invalid ROM size: %d\n", romsize);
	}

	if (ramsize < 6)
	{
		const byte ramsize_table[] = {1, 1, 1, 4, 16, 8};
		cart.ramsize = ramsize_table[ramsize];
	}
	else
	{
		MESSAGE_ERROR("Invalid RAM size: %d\n", ramsize);
		cart.ramsize = 1;
	}

	cart.sram = malloc(8192 * cart.ramsize);

	if (!cart.sram)
	{
		return -2;
	}

	MESSAGE_INFO("Cart loaded: name='%s', cgb=%d, sgb=%d, cart.mbc=%s, romsize=%dK, ramsize=%dK\n",
		cart.name, cart.cgb, cart.sgb, mbc_names[cart.mbctype], cart.romsize * 16, cart.ramsize * 8);

	// For DMG games we check if a GBC or SGB colorization palette is available
	if (cart.cgb != 3)
	{
		const uint16_t *bgp, *obp0, *obp1;
		uint8_t infoIdx = 0, checksum = 0;

		// Calculate the checksum over 16 title bytes.
		for (int i = 0; i < 16; i++) {
			checksum += header[0x0134 + i];
		}

		// Check if the checksum is in the list.
		for (size_t idx = 0; idx < sizeof(colorization_checksum); idx++) {
			if (colorization_checksum[idx] == checksum) {
				infoIdx = idx;

				// Indexes above 0x40 have to be disambiguated.
				if (idx > 0x40) {
					// No idea how that works. But it works.
					for (size_t i = idx - 0x41, j = 0; i < sizeof(colorization_disambig_chars); i += 14, j += 14) {
						if (header[0x0137] == colorization_disambig_chars[i]) {
							infoIdx += j;
							break;
						}
					}
				}
				break;
			}
		}

		uint8_t palette = colorization_palette_info[infoIdx] & 0x1F;
		uint8_t flags = (colorization_palette_info[infoIdx] & 0xE0) >> 5;

		bgp  = dmg_game_palettes[palette][2];
		obp0 = dmg_game_palettes[palette][(flags & 1) ? 0 : 1];
		obp1 = dmg_game_palettes[palette][(flags & 2) ? 0 : 1];

		if (!(flags & 4)) {
			obp1 = dmg_game_palettes[palette][2];
		}

		memcpy(&cart.colorize[0], bgp, 8); // BGP
		memcpy(&cart.colorize[1], bgp, 8); // BGP
		memcpy(&cart.colorize[2], obp0, 8); // OBP0
		memcpy(&cart.colorize[3], obp1, 8); // OBP1

		MESSAGE_INFO("Detected built-in GBC palette %d\n", palette);
	}

	hw.cgb = (cart.cgb == 2 || cart.cgb == 3);

	// Gameboy color games can be very large so we only load 1024K for faster boot
	// Also 4/8MB games do not fully fit, our bank manager takes care of swapping.

	int load_banks = cart.romsize;

	if (preload || cart.romsize <= 64)
	{
		load_banks = cart.romsize;
	}
	else if ((strncmp(cart.name, "RAYMAN", 6) == 0 || strncmp(cart.name, "NONAME", 6) == 0))
	{
		load_banks = cart.romsize - 40;
	}

	MESSAGE_INFO("Preloading the first %d banks\n", load_banks);
	for (int i = 0; i < load_banks; i++)
	{
		cart.mbc.rombank = i;
		mem_updatemap();
	}

	// Close the file if we no longer need it
	if (load_banks == cart.romsize)
	{
		fclose(cart.fpRomFile);
		cart.fpRomFile = NULL;
	}

	// Apply game-specific hacks
	if (strncmp(cart.name, "SIREN GB2 ", 11) == 0 || strncmp(cart.name, "DONKEY KONG", 11) == 0)
	{
		MESSAGE_INFO("HACK: Window offset hack enabled\n");
		lcd.enable_window_offset_hack = 1;
	}

	return 0;
}


void gnuboy_free_rom(void)
{
	if (cart.fpRomFile)
		fclose(cart.fpRomFile);
	for (int i = 0; i < 512; i++)
		free(cart.rombanks[i]);
	free(cart.sram);
	memset(&cart, 0, sizeof(cart));
	memset(&cart.mbc, 0, sizeof(cart.mbc));
}


int gnuboy_load_bios(const char *file)
{
	gnuboy_free_bios();

	if (!file)
	{
		MESSAGE_INFO("BIOS won't be used!\n");
		return 0;
	}

	FILE *fp = fopen(file, "rb");
	if (fp == NULL)
	{
		MESSAGE_ERROR("BIOS file '%s' not found\n", file);
		return -1;
	}

	MESSAGE_INFO("Loading BIOS file: '%s'\n", file);

	hw.bios = malloc(0x900);
	if (!hw.bios)
		gnuboy_panic("Out of memory");

	size_t cnt = fread(hw.bios, 1, 0x900, fp);
	fclose(fp);

	return (cnt >= 0x100) ? 0 : -2;
}


void gnuboy_free_bios(void)
{
	free(hw.bios);
	hw.bios = NULL;
}


void gnuboy_set_pad(uint32_t new_pad)
{
	if (new_pad != hw.pad)
	{
		hw.pad = new_pad;
		hw_pad_refresh();
	}
}


void gnuboy_set_btn(int btn, bool pressed)
{
	un32 new_pad = hw.pad;

	if (pressed)
		new_pad |= btn;
	else
		new_pad &= ~btn;

	gnuboy_set_pad(new_pad);
}


void gnuboy_set_pal(int palette)
{
	pal_set_dmg(palette);
}


void gnuboy_set_rtc(time_t epoch)
{
	struct tm *info = localtime(&epoch);

	rtc.d = info->tm_yday;
	rtc.h = info->tm_hour;
	rtc.m = info->tm_min;
	rtc.s = info->tm_sec;

	MESSAGE_INFO("Clock set to day %03d at %02d:%02d:%02d\n", rtc.d, rtc.h, rtc.m, rtc.s);
}
