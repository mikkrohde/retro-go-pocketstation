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
** nes/rom.h: ROM loader header
**
*/

#ifndef _NES_ROM_H_
#define _NES_ROM_H_

#define  ROM_INES_MAGIC          "NES\x1A"
#define  FDS_HEAD_MAGIC          "FDS\x1A"
#define  FDS_DISK_MAGIC          "\x01*NINTENDO-HVC*"

#define  ROM_FLAG_FOURSCREEN     0x08
#define  ROM_FLAG_TRAINER        0x04
#define  ROM_FLAG_BATTERY        0x02
#define  ROM_FLAG_VERTICAL       0x01
#define  ROM_FLAG_FREE_DATA      0x100

#define  ROM_PRG_BANK_SIZE       0x2000
#define  ROM_CHR_BANK_SIZE       0x2000

#define  TRAINER_OFFSET          0x1000
#define  TRAINER_LENGTH          0x200

typedef struct __attribute__((packed))
{
   uint8_t magic[4];
   uint8_t prg_banks;
   uint8_t chr_banks;
   uint8_t rom_type;
   uint8_t mapper_hinybble;
   uint32_t reserved1;
   uint32_t reserved2;
} inesheader_t;

typedef struct __attribute__((packed))
{
   uint8_t magic[4];
   uint8_t sides;
   uint8_t reserved[11];
} fdsheader_t;

typedef struct
{
   char filename[PATH_MAX + 1];

   uint8_t *data_ptr; // Top of our allocation
   size_t data_len; // Size of our allocation

   uint8_t *prg_rom;
   uint8_t *chr_rom;
   uint8_t *prg_ram;
   uint8_t *chr_ram;

   int prg_rom_banks;
   int chr_rom_banks;
   int prg_ram_banks;
   int chr_ram_banks;

   uint32_t mapper_number;
   uint32_t flags;
   uint32_t system;
   uint32_t mirroring;
   uint32_t checksum;
} rom_t;


extern rom_t *rom_loadfile(const char *filename);
extern rom_t *rom_loadmem(uint8_t *data, size_t size);
extern void rom_free(void);

#endif /* _NES_ROM_H_ */
