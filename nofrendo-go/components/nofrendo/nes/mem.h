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
** nes/mem.h: Memory emulation header
**
*/

#ifndef _NES_MEM_H_
#define _NES_MEM_H_

#define MEM_PAGEBITS  5
#define MEM_PAGESHIFT (16 - MEM_PAGEBITS)
#define MEM_PAGECOUNT (1 << MEM_PAGEBITS)
#define MEM_PAGESIZE  (0x10000 / MEM_PAGECOUNT)
#define MEM_PAGEMASK  (MEM_PAGESIZE - 1)

#define MEM_RAMSIZE   0x800

// This is kind of a hack, but for speed...
#define MEM_PAGE_USE_HANDLERS ((uint8_t*)1)
#define MEM_PAGE_READ_ONLY    ((uint8_t*)2)
#define MEM_PAGE_WRITE_ONLY   ((uint8_t*)3)

#define MEM_PAGE_HAS_HANDLERS(page) ((page) == MEM_PAGE_USE_HANDLERS)
#define MEM_PAGE_IS_VALID_PTR(page) ((page) > ((uint8_t*)100))

#define MEM_HANDLERS_MAX     32

#define LAST_MEMORY_HANDLER  { -1, -1, NULL }

typedef struct
{
   uint32_t min_range, max_range;
   uint8_t (*read_func)(uint32_t address);
} mem_read_handler_t;

typedef struct
{
   uint32_t min_range, max_range;
   void (*write_func)(uint32_t address, uint8_t value);
} mem_write_handler_t;

typedef struct
{
   // System RAM
   uint8_t ram[MEM_RAMSIZE];

   /* Plain memory (RAM/ROM) pages */
   uint8_t *pages[MEM_PAGECOUNT];

   /* Mostly for nes6502 fastmem functions */
   uint8_t *pages_read[MEM_PAGECOUNT];
   uint8_t *pages_write[MEM_PAGECOUNT];

   /* Special memory handlers */
   mem_read_handler_t read_handlers[MEM_HANDLERS_MAX];
   mem_write_handler_t write_handlers[MEM_HANDLERS_MAX];
} mem_t;

extern mem_t *mem_create(void);
extern void mem_shutdown(void);
extern void mem_reset(void);
extern void mem_setpage(uint32_t page, uint8_t *ptr);
extern uint8_t *mem_getpage(uint32_t page);

extern uint8_t mem_getbyte(uint32_t address);
extern uint32_t mem_getword(uint32_t address); // uint16_t
extern void mem_putbyte(uint32_t address, uint8_t value);

#endif
