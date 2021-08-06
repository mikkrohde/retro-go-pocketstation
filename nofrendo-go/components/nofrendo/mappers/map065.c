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
** map065.c: Irem H-3001 mapper interface
**
*/

#include <nofrendo.h>
#include <mmc.h>

static struct
{
   uint16_t counter;
   uint16_t cycles;
   uint8_t low, high;
   bool enabled;
} irq;


static void map65_init(rom_t *cart)
{
   UNUSED(cart);

   irq.counter = 0;
   irq.enabled = false;
   irq.low = irq.high = 0;
   irq.cycles = 0;
}

/* TODO: shouldn't there be some kind of HBlank callback??? */

static void map65_write(uint32_t address, uint8_t value)
{
   int range = address & 0xF000;
   int reg = address & 7;

   switch (range)
   {
   case 0x8000:
   case 0xA000:
   case 0xC000:
      mmc_bankrom(8, range, value);
      break;

   case 0xB000:
      mmc_bankvrom(1, reg << 10, value);
      break;

   case 0x9000:
      switch (reg)
      {
      case 4:
         irq.enabled = (value & 0x01) ? false : true;
         break;

      case 5:
         irq.high = value;
         irq.cycles = (irq.high << 8) | irq.low;
         irq.counter = (uint8_t)(irq.cycles / 128);
         break;

      case 6:
         irq.low = value;
         irq.cycles = (irq.high << 8) | irq.low;
         irq.counter = (uint8_t)(irq.cycles / 128);
         break;

      default:
         break;
      }
      break;

   default:
      break;
   }
}

static const mem_write_handler_t map65_memwrite[] =
{
   { 0x8000, 0xFFFF, map65_write },
   LAST_MEMORY_HANDLER
};

mapintf_t map65_intf =
{
   65,               /* mapper number */
   "Irem H-3001",    /* mapper name */
   map65_init,       /* init routine */
   NULL,             /* vblank callback */
   NULL,             /* hblank callback */
   NULL,             /* get state (snss) */
   NULL,             /* set state (snss) */
   NULL,             /* memory read structure */
   map65_memwrite,   /* memory write structure */
   NULL              /* external sound device */
};
