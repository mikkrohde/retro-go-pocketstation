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
** nes/input.h: Input emulation header
**
*/

#ifndef _NESINPUT_H_
#define _NESINPUT_H_

/* Registers */
#define INP_REG_JOY0 0x4016
#define INP_REG_JOY1 0x4017

/* NES control pad bitmasks */
#define INP_PAD_A 0x01
#define INP_PAD_B 0x02
#define INP_PAD_SELECT 0x04
#define INP_PAD_START 0x08
#define INP_PAD_UP 0x10
#define INP_PAD_DOWN 0x20
#define INP_PAD_LEFT 0x40
#define INP_PAD_RIGHT 0x80

#define INP_ZAPPER_HIT 0x01
#define INP_ZAPPER_MISS 0x02
#define INP_ZAPPER_TRIG 0x04

typedef enum
{
    INP_JOYPAD0,
    INP_JOYPAD1,
    INP_JOYPAD2,
    INP_JOYPAD3,
    INP_ZAPPER,
    INP_TYPE_MAX,
} nesinput_type_t;

typedef struct
{
    uint8_t connected;
    uint8_t state;
    uint8_t reads;
} nesinput_t;

extern void input_connect(nesinput_type_t input);
extern void input_disconnect(nesinput_type_t input);
extern void input_update(nesinput_type_t input, uint8_t state);

extern uint8_t input_read(uint32_t address);
extern void input_write(uint32_t address, uint8_t value);

#endif /* _NESINPUT_H_ */
