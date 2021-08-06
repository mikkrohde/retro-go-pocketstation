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
** map042.c: Mapper #42 (Baby Mario bootleg)
** Implemented by Firebug with information courtesy of Kevin Horton
**
*/

#include <nofrendo.h>
#include <mmc.h>
#include <nes.h>

static struct
{
   uint16_t enabled;
   uint16_t counter;
} irq;

/********************************/
/* Mapper #42 IRQ reset routine */
/********************************/
static void map42_irq_reset (void)
{
  /* Turn off IRQs */
  irq.enabled = false;
  irq.counter = 0x0000;
}

/********************************************/
/* Mapper #42: Baby Mario bootleg cartridge */
/********************************************/
static void map42_init(rom_t *cart)
{
   UNUSED(cart);

   /* Set the hardwired pages */
   mmc_bankrom (8, 0x8000, 0x0C);
   mmc_bankrom (8, 0xA000, 0x0D);
   mmc_bankrom (8, 0xC000, 0x0E);
   mmc_bankrom (8, 0xE000, 0x0F);

   /* Reset the IRQ counter */
   map42_irq_reset();
}

/****************************************/
/* Mapper #42 callback for IRQ handling */
/****************************************/
static void map42_hblank(int scanline)
{
   UNUSED(scanline);

   /* Increment the counter if it is enabled and check for strike */
   if (irq.enabled)
   {
      irq.counter += NES_CYCLES_PER_SCANLINE;

      /* IRQ is triggered after 24576 M2 cycles */
      if (irq.counter >= 0x6000)
      {
         /* Trigger the IRQ */
         nes6502_irq();

         /* Reset the counter */
         map42_irq_reset();
      }
   }
}

/******************************************/
/* Mapper #42 write handler ($E000-$FFFF) */
/******************************************/
static void map42_write(uint32_t address, uint8_t value)
{
   switch (address & 0x03)
   {
      /* Register 0: Select ROM page at $6000-$7FFF */
      case 0x00:
         mmc_bankrom(8, 0x6000, value & 0x0F);
         break;

      /* Register 1: mirroring */
      case 0x01:
         if (value & 0x08) ppu_setmirroring(PPU_MIRROR_HORI);
         else              ppu_setmirroring(PPU_MIRROR_VERT);
         break;

      /* Register 2: IRQ */
      case 0x02:
         if (value & 0x02) irq.enabled = true;
         else              map42_irq_reset();
         break;

      /* Register 3: unused */
      default:
         break;
   }
}

static const mem_write_handler_t map42_memwrite[] =
{
   { 0xE000, 0xFFFF, map42_write },
   LAST_MEMORY_HANDLER
};

mapintf_t map42_intf =
{
   42,                      /* Mapper number */
   "Baby Mario (bootleg)",  /* Mapper name */
   map42_init,              /* Initialization routine */
   NULL,                    /* VBlank callback */
   map42_hblank,            /* HBlank callback */
   NULL,                    /* Get state (SNSS) */
   NULL,                    /* Set state (SNSS) */
   NULL,                    /* Memory read structure */
   map42_memwrite,          /* Memory write structure */
   NULL                     /* External sound device */
};
