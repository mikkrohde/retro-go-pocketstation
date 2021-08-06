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
** map020.c: Famicom Disk System
** Implemented by ducalex with the help of wiki.nesdev.com and FCEUX
**
** Mapper 20 is reserved by iNES for internal usage when emulating an FDS game.
** The FDS doesn't actually use a mapper, but our mapper infrastructure is a
** convenient place to hook into the memory map, sound, and save state systems.
*/

#include <nofrendo.h>
#include <string.h>
#include <mmc.h>
#include <nes.h>

#define FDS_CLOCK (NES_CPU_CLOCK_NTSC / 2)
#define SEEK_TIME 100 // 150
#define CLEAR_IRQ() irq.seek_counter = irq.transfer_done = irq.timer_fired = 0; nes6502_irq_clear();
#define TIMER_RELOAD() irq.timer_counter = (fds->regs[1] << 8) | fds->regs[0];

#define REG2_IRQ_REPEAT         (1 << 0)
#define REG2_IRQ_ENABLED        (1 << 1)
#define REG3_ENABLE_DISK_REGS   (1 << 0)
#define REG3_ENABLE_SOUND_REGS  (1 << 1)
#define REG5_MOTOR_ON           (1 << 0)
#define REG5_TRANSFER_RESET     (1 << 1)
#define REG5_READ_MODE          (1 << 2)
#define REG5_MIRRORING          (1 << 3)
#define REG5_CRC_CONTROL        (1 << 4)
#define REG5_TRANSFER_START     (1 << 6)
#define REG5_USE_INTERRUPT      (1 << 7)

enum
{
    BLOCK_INIT = 0,
    BLOCK_VOLUME,
    BLOCK_FILECOUNT,
    BLOCK_FILEHEADER,
    BLOCK_FILEDATA,
    BLOCK_NEXT,
};

typedef struct
{
    uint8_t *disk[8];
    uint8_t sides;
    uint8_t regs[8];
    uint8_t *block_ptr;
    int block_type;
    int block_pos;
    int block_size;
    int block_filesize;
} fds_t;

struct
{
    // Timer IRQ
    long timer_counter;
    bool timer_fired;
    // Disk access IRQ
    long seek_counter;
    bool transfer_done;
} irq;

static fds_t *fds;


static void fds_cpu_timer(int cycles)
{
    if (irq.timer_counter > 0 && (fds->regs[2] & REG2_IRQ_ENABLED))
    {
        irq.timer_counter -= cycles;
        if (irq.timer_counter <= 0)
        {
            if (!(fds->regs[2] & REG2_IRQ_REPEAT))
                fds->regs[2] &= ~REG2_IRQ_ENABLED;

            TIMER_RELOAD();
            nes6502_irq();

            irq.timer_fired = true;
        }
    }

    if (irq.seek_counter > 0)
    {
        irq.seek_counter -= cycles;
        if (irq.seek_counter <= 0)
        {
            if (fds->regs[5] & REG5_USE_INTERRUPT)
            {
                nes6502_irq();
            }
            irq.transfer_done = true;
        }
    }
}

static void fds_hblank(int scanline)
{
    fds_cpu_timer(NES_CYCLES_PER_SCANLINE);
}

static uint8_t fds_read(uint32_t address)
{
    // MESSAGE_INFO("FDS read at %04X\n", address);
    uint8_t ret = 0;

    switch (address)
    {
        case 0x4030:                    // Disk Status Register 0
            if (irq.timer_fired) ret |= 1;        // Timer IRQ
            if (irq.transfer_done) ret |= 2;     // Byte transferred
            CLEAR_IRQ();
            return ret;

        case 0x4031:                    // Read data register
            if (fds->regs[5] & 0x04)
            {
                CLEAR_IRQ();
                irq.seek_counter = SEEK_TIME;

                if (fds->block_pos < fds->block_size)
                    return fds->block_ptr[fds->block_pos++];
                else
                    return 0;
            }
            return 0xFF;

        case 0x4032:                    // Disk drive status register
            // wprotect|/ready|/inserted
            if (!(fds->regs[5] & 1) || (fds->regs[5] & 2))
                return 0b110;
            return 0b100;

        case 0x4033:                    // External connector read
            // bit 7 = battery, rest = ignore for now
            return 0x80;

        default:
            return 0x00;
    }
}

static void fds_write(uint32_t address, uint8_t value)
{
    // MESSAGE_INFO("FDS write at %04X: %02X\n", address, value);

    switch (address)
    {
        case 0x4020:                    // IRQ reload value low
            CLEAR_IRQ();
            break;

        case 0x4021:                    // IRQ reload value high
            CLEAR_IRQ();
            break;

        case 0x4022:                    // IRQ control
            CLEAR_IRQ();
            if (value & REG2_IRQ_ENABLED)
                TIMER_RELOAD();
            break;

        case 0x4023:                    // Master I/O enable
            // Do nothing
            break;

        case 0x4024:                    // Write data register
            break;

        case 0x4025:                    // FDS Control
            // Transfer Reset
            if (value & 0x02)
            {
                fds->block_type = BLOCK_INIT;
                fds->block_ptr = &fds->disk[0][0];
                fds->block_filesize = 0;
                fds->block_size = 0;
                fds->block_pos = 0;
            }

            // New transfer
            if (value & 0x40 && ~(fds->regs[5]) & 0x40)
            {
                fds->block_ptr = fds->block_ptr + fds->block_pos;
                fds->block_pos = 0;

                switch (fds->block_type + 1)
                {
                    case BLOCK_VOLUME:
                        fds->block_type = BLOCK_VOLUME;
                        fds->block_size = 0x38;
                        break;
                    case BLOCK_FILECOUNT:
                        fds->block_type = BLOCK_FILECOUNT;
                        fds->block_size = 0x02;
                        break;
                    case BLOCK_FILEHEADER:
                    case BLOCK_NEXT:
                        fds->block_type = BLOCK_FILEHEADER;
                        fds->block_size = 0x10;
                        fds->block_filesize = (fds->block_ptr[13]) | (fds->block_ptr[14]) << 8;
                        break;
                    case BLOCK_FILEDATA:
                        fds->block_type = BLOCK_FILEDATA;
                        fds->block_size = 0x01 + fds->block_filesize;
                        break;
                }

                MESSAGE_INFO("Block type %d with size %d bytes\n", fds->block_type, fds->block_size);
            }

            // Turn on motor
            if (value & 0x40)
            {
                irq.seek_counter = SEEK_TIME;
            }

            // Update mirroring
            if ((value & REG5_MIRRORING) != (fds->regs[5] & REG5_MIRRORING) || fds->regs[5] == 0)
            {
                ppu_setmirroring((value & REG5_MIRRORING) ? PPU_MIRROR_HORI : PPU_MIRROR_VERT);
            }

            break;
    }

    fds->regs[address & 7] = value;
}

static uint8_t fds_sound_read(uint32_t address)
{
    MESSAGE_INFO("FDS sound read at %04X\n", address);
    return 0x00;
}

static void fds_sound_write(uint32_t address, uint8_t value)
{
    MESSAGE_INFO("FDS sound write at %04X: %02X\n", address, value);
}

static uint8_t fds_wave_read(uint32_t address)
{
    MESSAGE_INFO("FDS wave read at %04X\n", address);
    return 0x00;
}

static void fds_wave_write(uint32_t address, uint8_t value)
{
    MESSAGE_INFO("FDS wave write at %04X: %02X\n", address, value);
}

static void fds_getstate(void *state)
{
    //
}

static void fds_setstate(void *state)
{
    //
}

void fds_init(rom_t *cart)
{
    if (!fds)
    {
        fds = calloc(1, sizeof(fds_t));

        // I'm not sure yet where we should load the bios
        // so it shall be hardcoded here while I work on
        // the actual hardware emulation...
        FILE *fp = fopen("/sd/roms/fds/disksys.rom", "rb");
        fread(cart->prg_rom, 0x2000, 1, fp);
        fclose(fp);
    }

    uint8_t *disk_ptr = cart->data_ptr;

    if (memcmp(disk_ptr, FDS_HEAD_MAGIC, 4) == 0)
    {
        fds->sides = ((fdsheader_t *)disk_ptr)->sides;
        disk_ptr += 16;
    }
    else
    {
        fds->sides = cart->data_len / 65500;
    }

    fds->sides = MIN(MAX(fds->sides, 1), 8);

    for (int i = 0; i < fds->sides; i++)
    {
        fds->disk[i] = disk_ptr + (i * 65500);
    }
    fds->block_ptr = &fds->disk[0][0];

    mmc_bankprg(32, 0x6000, 0, PRG_RAM); // PRG-RAM 0x6000-0xDFFF
    mmc_bankprg(8,  0xE000, 0, PRG_ROM); // BIOS 0xE000-0xFFFF

    ppu_setmirroring(PPU_MIRROR_HORI);
    // nes_settimer(fds_cpu_timer, 10);
}

static const mem_read_handler_t fds_memread[] =
{
    {0x4030, 0x4035, fds_read},
    {0x4040, 0x407F, fds_wave_read},
    {0x4090, 0x4092, fds_sound_read},
    LAST_MEMORY_HANDLER
};

static const mem_write_handler_t fds_memwrite[] =
{
    {0x4020, 0x4025, fds_write},
    {0x4040, 0x407F, fds_wave_write},
    {0x4080, 0x408A, fds_sound_write},
    LAST_MEMORY_HANDLER
};

mapintf_t map20_intf =
{
    20,                     /* mapper number */
    "Famicom Disk System",  /* mapper name */
    fds_init,               /* init routine */
    NULL,                   /* vblank callback */
    fds_hblank,             /* hblank callback */
    fds_getstate,           /* get state (snss) */
    fds_setstate,           /* set state (snss) */
    fds_memread,            /* memory read structure */
    fds_memwrite,           /* memory write structure */
    NULL                    /* external sound device */
};
