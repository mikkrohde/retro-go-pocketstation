#include "gnuboy.h"
#include "sound.h"
#include "cpu.h"
#include "hw.h"
#include "regs.h"

#include "sound_luts.h"

pcm_t pcm;
snd_t snd;

#define RATE (snd.rate)
#define WAVE (snd.wave) /* ram.hi+0x30 */
#define S1 (snd.ch[0])
#define S2 (snd.ch[1])
#define S3 (snd.ch[2])
#define S4 (snd.ch[3])

#define s1_freq() {int d = 2048 - (((R_NR14&7)<<8) + R_NR13); S1.freq = (RATE > (d<<4)) ? 0 : (RATE << 17)/d;}
#define s2_freq() {int d = 2048 - (((R_NR24&7)<<8) + R_NR23); S2.freq = (RATE > (d<<4)) ? 0 : (RATE << 17)/d;}
#define s3_freq() {int d = 2048 - (((R_NR34&7)<<8) + R_NR33); S3.freq = (RATE > (d<<3)) ? 0 : (RATE << 21)/d;}
#define s4_freq() {S4.freq = (freqtab[R_NR43&7] >> (R_NR43 >> 4)) * RATE; if (S4.freq >> 18) S4.freq = 1<<18;}


static inline void s1_init()
{
	S1.swcnt = 0;
	S1.swfreq = ((R_NR14&7)<<8) + R_NR13;
	S1.envol = R_NR12 >> 4;
	S1.endir = (R_NR12>>3) & 1;
	S1.endir |= S1.endir - 1;
	S1.enlen = (R_NR12 & 7) << 15;
	if (!S1.on) S1.pos = 0;
	S1.on = 1;
	S1.cnt = 0;
	S1.encnt = 0;
}

static inline void s2_init()
{
	S2.envol = R_NR22 >> 4;
	S2.endir = (R_NR22>>3) & 1;
	S2.endir |= S2.endir - 1;
	S2.enlen = (R_NR22 & 7) << 15;
	if (!S2.on) S2.pos = 0;
	S2.on = 1;
	S2.cnt = 0;
	S2.encnt = 0;
}

static inline void s3_init()
{
	if (!S3.on) S3.pos = 0;
	S3.cnt = 0;
	S3.on = R_NR30 >> 7;
	if (!S3.on) return;
	for (int i = 0; i < 16; i++)
		hw.himem[i+0x30] = 0x13 ^ hw.himem[i+0x31];
}

static inline void s4_init()
{
	S4.envol = R_NR42 >> 4;
	S4.endir = (R_NR42>>3) & 1;
	S4.endir |= S4.endir - 1;
	S4.enlen = (R_NR42 & 7) << 15;
	S4.on = 1;
	S4.pos = 0;
	S4.cnt = 0;
	S4.encnt = 0;
}

void sound_dirty()
{
	S1.swlen = ((R_NR10>>4) & 7) << 14;
	S1.len = (64-(R_NR11&63)) << 13;
	S1.envol = R_NR12 >> 4;
	S1.endir = (R_NR12>>3) & 1;
	S1.endir |= S1.endir - 1;
	S1.enlen = (R_NR12 & 7) << 15;
	s1_freq();

	S2.len = (64-(R_NR21&63)) << 13;
	S2.envol = R_NR22 >> 4;
	S2.endir = (R_NR22>>3) & 1;
	S2.endir |= S2.endir - 1;
	S2.enlen = (R_NR22 & 7) << 15;
	s2_freq();

	S3.len = (256-R_NR31) << 20;
	s3_freq();

	S4.len = (64-(R_NR41&63)) << 13;
	S4.envol = R_NR42 >> 4;
	S4.endir = (R_NR42>>3) & 1;
	S4.endir |= S4.endir - 1;
	S4.enlen = (R_NR42 & 7) << 15;
	s4_freq();
}

void sound_off()
{
	memset(&S1, 0, sizeof S1);
	memset(&S2, 0, sizeof S2);
	memset(&S3, 0, sizeof S3);
	memset(&S4, 0, sizeof S4);
	R_NR10 = 0x80;
	R_NR11 = 0xBF;
	R_NR12 = 0xF3;
	R_NR14 = 0xBF;
	R_NR21 = 0x3F;
	R_NR22 = 0x00;
	R_NR24 = 0xBF;
	R_NR30 = 0x7F;
	R_NR31 = 0xFF;
	R_NR32 = 0x9F;
	R_NR34 = 0xBF;
	R_NR41 = 0xFF;
	R_NR42 = 0x00;
	R_NR43 = 0x00;
	R_NR44 = 0xBF;
	R_NR50 = 0x77;
	R_NR51 = 0xF3;
	R_NR52 = 0x70;
	sound_dirty();
}

int sound_init(int sample_rate, bool stereo)
{
	pcm.hz = sample_rate;
	pcm.stereo = stereo;

	return 0;
}

void sound_reset(bool hard)
{
	memset(&snd, 0, sizeof snd);
	memcpy(WAVE, hw.cgb ? cgbwave : dmgwave, 16);
	memcpy(hw.himem + 0x30, WAVE, 16);
	snd.rate = pcm.hz ? (int)(((1<<21) / (double)pcm.hz) + 0.5) : 0;
	pcm.pos = 0;
	sound_off();
	R_NR52 = 0xF1;
}

void sound_mix()
{
	if (!RATE || snd.cycles < RATE)
		return;

	for (; snd.cycles >= RATE; snd.cycles -= RATE)
	{
		int l = 0;
		int r = 0;

		if (S1.on)
		{
			int s = sqwave[R_NR11>>6][(S1.pos>>18)&7] & S1.envol;
			S1.pos += S1.freq;

			if ((R_NR14 & 64) && ((S1.cnt += RATE) >= S1.len))
				S1.on = 0;

			if (S1.enlen && (S1.encnt += RATE) >= S1.enlen)
			{
				S1.encnt -= S1.enlen;
				S1.envol += S1.endir;
				if (S1.envol < 0) S1.envol = 0;
				if (S1.envol > 15) S1.envol = 15;
			}

			if (S1.swlen && (S1.swcnt += RATE) >= S1.swlen)
			{
				S1.swcnt -= S1.swlen;
				int f = S1.swfreq;

				if (R_NR10 & 8)
					f -= (f >> (R_NR10 & 7));
				else
					f += (f >> (R_NR10 & 7));

				if (f > 2047)
					S1.on = 0;
				else
				{
					S1.swfreq = f;
					R_NR13 = f;
					R_NR14 = (R_NR14 & 0xF8) | (f>>8);
					s1_freq();
				}
			}
			s <<= 2;
			if (R_NR51 & 1) r += s;
			if (R_NR51 & 16) l += s;
		}

		if (S2.on)
		{
			int s = sqwave[R_NR21>>6][(S2.pos>>18)&7] & S2.envol;
			S2.pos += S2.freq;

			if ((R_NR24 & 64) && ((S2.cnt += RATE) >= S2.len))
				S2.on = 0;

			if (S2.enlen && (S2.encnt += RATE) >= S2.enlen)
			{
				S2.encnt -= S2.enlen;
				S2.envol += S2.endir;
				if (S2.envol < 0) S2.envol = 0;
				if (S2.envol > 15) S2.envol = 15;
			}
			s <<= 2;
			if (R_NR51 & 2) r += s;
			if (R_NR51 & 32) l += s;
		}

		if (S3.on)
		{
			int s = WAVE[(S3.pos>>22) & 15];

			if (S3.pos & (1<<21))
				s &= 15;
			else
				s >>= 4;

			s -= 8;
			S3.pos += S3.freq;

			if ((R_NR34 & 64) && ((S3.cnt += RATE) >= S3.len))
				S3.on = 0;

			if (R_NR32 & 96)
				s <<= (3 - ((R_NR32>>5)&3));
			else
				s = 0;

			if (R_NR51 & 4) r += s;
			if (R_NR51 & 64) l += s;
		}

		if (S4.on)
		{
			int s;

			if (R_NR43 & 8)
				s = 1 & (noise7[(S4.pos>>20)&15] >> (7-((S4.pos>>17)&7)));
			else
				s = 1 & (noise15[(S4.pos>>20)&4095] >> (7-((S4.pos>>17)&7)));

			s = (-s) & S4.envol;
			S4.pos += S4.freq;

			if ((R_NR44 & 64) && ((S4.cnt += RATE) >= S4.len))
				S4.on = 0;

			if (S4.enlen && (S4.encnt += RATE) >= S4.enlen)
			{
				S4.encnt -= S4.enlen;
				S4.envol += S4.endir;
				if (S4.envol < 0) S4.envol = 0;
				if (S4.envol > 15) S4.envol = 15;
			}

			s += s << 1;

			if (R_NR51 & 8) r += s;
			if (R_NR51 & 128) l += s;
		}

		l *= (R_NR50 & 0x07);
		r *= ((R_NR50 & 0x70)>>4);
		// l >>= 4;
		// r >>= 4;

		l <<= 4;
		r <<= 4;

		// if (l > 127) l = 127;
		// else if (l < -128) l = -128;
		// if (r > 127) r = 127;
		// else if (r < -128) r = -128;

		if (pcm.buf)
		{
			if (pcm.pos >= pcm.len)
			{
				//pcm_submit();
				MESSAGE_ERROR("buffer overflow. (pcm.len=%d)\n", pcm.len);
				//abort();
			}
			else if (pcm.stereo)
			{
				pcm.buf[pcm.pos++] = (n16)l; //+128;
				pcm.buf[pcm.pos++] = (n16)r; //+128;
			}
			else pcm.buf[pcm.pos++] = (n16)((l+r)>>1); //+128;
		}
	}
	R_NR52 = (R_NR52&0xf0) | S1.on | (S2.on<<1) | (S3.on<<2) | (S4.on<<3);
}

byte sound_read(byte r)
{
	sound_mix();
	return REG(r);
}

void sound_write(byte r, byte b)
{
	if (!(R_NR52 & 128) && r != RI_NR52)
		return;

	if ((r & 0xF0) == 0x30)
	{
		if (S3.on)
			sound_mix();
		if (!S3.on)
			WAVE[r-0x30] = REG(r) = b;
		return;
	}

	sound_mix();

	switch (r)
	{
	case RI_NR10:
		R_NR10 = b;
		S1.swlen = ((R_NR10>>4) & 7) << 14;
		S1.swfreq = ((R_NR14&7)<<8) + R_NR13;
		break;
	case RI_NR11:
		R_NR11 = b;
		S1.len = (64-(R_NR11&63)) << 13;
		break;
	case RI_NR12:
		R_NR12 = b;
		S1.envol = R_NR12 >> 4;
		S1.endir = (R_NR12>>3) & 1;
		S1.endir |= S1.endir - 1;
		S1.enlen = (R_NR12 & 7) << 15;
		break;
	case RI_NR13:
		R_NR13 = b;
		s1_freq();
		break;
	case RI_NR14:
		R_NR14 = b;
		s1_freq();
		if (b & 128) s1_init();
		break;
	case RI_NR21:
		R_NR21 = b;
		S2.len = (64-(R_NR21&63)) << 13;
		break;
	case RI_NR22:
		R_NR22 = b;
		S2.envol = R_NR22 >> 4;
		S2.endir = (R_NR22>>3) & 1;
		S2.endir |= S2.endir - 1;
		S2.enlen = (R_NR22 & 7) << 15;
		break;
	case RI_NR23:
		R_NR23 = b;
		s2_freq();
		break;
	case RI_NR24:
		R_NR24 = b;
		s2_freq();
		if (b & 128) s2_init();
		break;
	case RI_NR30:
		R_NR30 = b;
		if (!(b & 128)) S3.on = 0;
		break;
	case RI_NR31:
		R_NR31 = b;
		S3.len = (256-R_NR31) << 13;
		break;
	case RI_NR32:
		R_NR32 = b;
		break;
	case RI_NR33:
		R_NR33 = b;
		s3_freq();
		break;
	case RI_NR34:
		R_NR34 = b;
		s3_freq();
		if (b & 128) s3_init();
		break;
	case RI_NR41:
		R_NR41 = b;
		S4.len = (64-(R_NR41&63)) << 13;
		break;
	case RI_NR42:
		R_NR42 = b;
		S4.envol = R_NR42 >> 4;
		S4.endir = (R_NR42>>3) & 1;
		S4.endir |= S4.endir - 1;
		S4.enlen = (R_NR42 & 7) << 15;
		break;
	case RI_NR43:
		R_NR43 = b;
		s4_freq();
		break;
	case RI_NR44:
		R_NR44 = b;
		if (b & 128) s4_init();
		break;
	case RI_NR50:
		R_NR50 = b;
		break;
	case RI_NR51:
		R_NR51 = b;
		break;
	case RI_NR52:
		R_NR52 = b;
		if (!(R_NR52 & 128))
			sound_off();
		break;
	default:
		return;
	}
}
