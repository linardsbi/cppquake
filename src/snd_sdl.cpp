
#include "SDL_audio.h"
#include "quakedef.hpp"

static dma_t the_shm;
static int snd_inited;

static void paint_audio(void *unused, Uint8 *stream, int len) {
    if (shm != nullptr) {
        shm->buffer = stream;
        shm->samplepos = shm->samplepos + len / (shm->samplebits / 8) / 2;
        // Check for samplepos overflow?
        S_PaintChannels(shm->samplepos);
    }
}

auto SNDDMA_Init() -> qboolean {
    // Ideally these would be specified in a config file
    // or passed as parameters to the executable.
    constexpr int desired_speed = 44100;
    constexpr int desired_bits = 16;
    constexpr int sound_channels = 2;
    constexpr int samples = 512;

    SDL_AudioSpec desired;

    snd_inited = 0;

    /* Set up the desired format */
    desired.freq = desired_speed;
    switch (desired_bits) {
        case 8:
            desired.format = AUDIO_U8;
            break;
        case 16:
            if constexpr (SDL_BYTEORDER == SDL_BIG_ENDIAN)
                desired.format = AUDIO_S16MSB;
            else
                desired.format = AUDIO_S16LSB;
            break;
        case 32:
            if constexpr (SDL_BYTEORDER == SDL_BIG_ENDIAN)
                desired.format = AUDIO_S32MSB;
            else
                desired.format = AUDIO_S32LSB;
            break;
        default:
            Con_Printf("Unknown number of audio bits: %d\n",
                       desired_bits);
            return false;
    }
    desired.channels = sound_channels;
    desired.samples = samples;
    desired.callback = paint_audio;

    /* Open the audio device */
    if (SDL_OpenAudio(&desired, nullptr) < 0) {
        Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
        return false;
    }

    SDL_PauseAudio(0);

    /* Fill the audio DMA information block */
    shm = &the_shm;
    shm->splitbuffer = false;
    shm->samplebits = (desired.format & 0xFF);
    shm->speed = desired.freq;
    shm->channels = desired.channels;
    shm->samples = desired.samples * shm->channels;
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = nullptr;

    snd_inited = 1;
    return true;
}

auto SNDDMA_GetDMAPos() -> int {
    return shm->samplepos;
}

void SNDDMA_Shutdown() {
    if (snd_inited) {
        SDL_CloseAudio();
        snd_inited = 0;
    }
}

