
#include <cstdio>
#include "SDL_audio.h"
#include "SDL_byteorder.h"
#include "quakedef.hpp"

static dma_t the_shm;
static int snd_inited;

extern int desired_speed;
extern int desired_bits;

static void paint_audio(void *unused, Uint8 *stream, int len)
{
	if ( shm ) {
		shm->buffer = stream;
		shm->samplepos += len/(shm->samplebits/8)/2;
		// Check for samplepos overflow?
		S_PaintChannels (shm->samplepos);
	}
}

auto SNDDMA_Init() -> qboolean
{
	SDL_AudioSpec desired, obtained;

	snd_inited = 0;

	/* Set up the desired format */
	desired.freq = desired_speed;
	switch (desired_bits) {
		case 8:
			desired.format = AUDIO_U8;
			break;
		case 16:
			if ( SDL_BYTEORDER == SDL_BIG_ENDIAN )
				desired.format = AUDIO_S16MSB;
			else
				desired.format = AUDIO_S16LSB;
			break;
		default:
        		Con_Printf("Unknown number of audio bits: %d\n",
								desired_bits);
			return false;
	}
	desired.channels = 2;
	desired.samples = 512;
	desired.callback = paint_audio;

	/* Open the audio device */
	if ( SDL_OpenAudio(&desired, &obtained) < 0 ) {
        	Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		return false;
	}

	/* Make sure we can support the audio format */
	switch (obtained.format) {
		case AUDIO_U8:
			/* Supported */
			break;
		case AUDIO_S16LSB:
		case AUDIO_S16MSB:
			if ( ((obtained.format == AUDIO_S16LSB) &&
			     (SDL_BYTEORDER == SDL_LIL_ENDIAN)) ||
			     ((obtained.format == AUDIO_S16MSB) &&
			     (SDL_BYTEORDER == SDL_BIG_ENDIAN)) ) {
				/* Supported */
				break;
			}
			/* Unsupported, fall through */;
		default:
			/* Not supported -- force SDL to do our bidding */
			SDL_CloseAudio();
			if ( SDL_OpenAudio(&desired, nullptr) < 0 ) {
        			Con_Printf("Couldn't open SDL audio: %s\n",
							SDL_GetError());
				return false;
			}
			memcpy(&obtained, &desired, sizeof(desired));
			break;
	}
	SDL_PauseAudio(0);

	/* Fill the audio DMA information block */
	shm = &the_shm;
	shm->splitbuffer = false;
	shm->samplebits = (obtained.format & 0xFF);
	shm->speed = obtained.freq;
	shm->channels = obtained.channels;
	shm->samples = obtained.samples*shm->channels;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = nullptr;

	snd_inited = 1;
	return true;
}

auto SNDDMA_GetDMAPos() -> int
{
	return shm->samplepos;
}

void SNDDMA_Shutdown()
{
	if (snd_inited)
	{
		SDL_CloseAudio();
		snd_inited = 0;
	}
}

