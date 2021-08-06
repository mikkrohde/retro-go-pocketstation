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
** nes/state.h: Save state support header
**
*/

#ifndef _NESSTATE_H_
#define _NESSTATE_H_

typedef struct
{
    uint8_t  type[4];
    uint32_t blockVersion;
    uint32_t blockLength;
} SnssBlockHeader;

extern int state_load(const char *fn);
extern int state_save(const char *fn);

#endif /* _NESSTATE_H_ */
