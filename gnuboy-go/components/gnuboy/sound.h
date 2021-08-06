#ifndef __SOUND_H__
#define __SOUND_H__

#include "gnuboy.h"

typedef struct
{
	struct {
		int on;
		unsigned pos;
		int cnt, encnt, swcnt;
		int len, enlen, swlen;
		int swfreq;
		int freq;
		int envol, endir;
	} ch[4];
	byte wave[16];
	int rate;
	int cycles;
} snd_t;

typedef struct
{
	int hz, len;
	int stereo;
	n16* buf;
	int pos;
} pcm_t;

extern pcm_t pcm;
extern snd_t snd;

int sound_init(int sample_rate, bool stereo);
void sound_write(byte r, byte b);
byte sound_read(byte r);
void sound_dirty();
void sound_reset(bool hard);
void sound_mix();

#endif
