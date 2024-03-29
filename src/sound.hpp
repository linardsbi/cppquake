/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sound.h -- client sound i/o functions

#ifndef __SOUND__
#define __SOUND__

#include "zone.hpp"

#define DEFAULT_SOUND_PACKET_VOLUME 255
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0

// !!! if this is changed, it much be changed in asm_i386.h too !!!
struct portable_samplepair_t {
    int left;
    int right;
};

typedef struct sfx_s {
    char name[MAX_QPATH];
    cache_user_t cache;
} sfx_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct {
    int length;
    int loopstart;
    int speed;
    int width;
    int stereo;
    byte data[1];        // variable sized
} sfxcache_t;

typedef struct {
    qboolean gamealive;
    qboolean soundalive;
    qboolean splitbuffer;
    int channels;
    int samples;                // mono samples in buffer
    int submission_chunk;        // don't mix less than this #
    int samplepos;                // in mono samples
    int samplebits;
    int speed;
    unsigned char *buffer;
} dma_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct {
    sfx_t *sfx;            // sfx number
    int leftvol;        // 0-255 volume
    int rightvol;        // 0-255 volume
    int end;            // end time in global paintsamples
    int pos;            // sample position in sfx
    int looping;        // where to loop, -1 = no looping
    int entnum;            // to allow overriding a specific sound
    int entchannel;        //
    vec3 origin;            // origin of sound effect
    vec_t dist_mult;        // distance multiplier (attenuation/clipK)
    int master_vol;        // 0-255 master volume
} channel_t;

typedef struct {
    int rate;
    int width;
    int channels;
    int loopstart;
    int samples;
    int dataofs;        // chunk starts this many bytes from file start
} wavinfo_t;

void S_Init();

void S_Startup();

void S_Shutdown();

void S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3 origin, float fvol, float attenuation);

void S_StaticSound(sfx_t *sfx, vec3 origin, float vol, float attenuation);

void S_StopSound(int entnum, int entchannel);

void S_StopAllSounds(qboolean clear);

void S_ClearBuffer();

void S_Update(vec3 origin, vec3 v_forward, vec3 v_right, vec3 v_up);

void S_ExtraUpdate();

sfx_t *S_PrecacheSound(std::string_view sample);

void S_TouchSound(char *sample);

void S_ClearPrecache();

void S_BeginPrecaching();

void S_EndPrecaching();

void S_PaintChannels(int endtime);

void S_InitPaintChannels();

// picks a channel based on priorities, empty slots, number of channels
channel_t *SND_PickChannel(int entnum, int entchannel);

// spatializes a channel
void SND_Spatialize(channel_t *ch);

// initializes cycling through a DMA buffer and returns information on it
qboolean SNDDMA_Init();

// gets the current DMA position
int SNDDMA_GetDMAPos();

// shutdown the DMA xfer.
void SNDDMA_Shutdown();

// ====================================================================
// User-setable variables
// ====================================================================

#define    MAX_CHANNELS            128
#define    MAX_DYNAMIC_CHANNELS    8


extern channel_t channels[MAX_CHANNELS];
// 0 to MAX_DYNAMIC_CHANNELS-1	= normal entity sounds
// MAX_DYNAMIC_CHANNELS to MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS -1 = water, etc
// MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS to total_channels = static sounds

extern int total_channels;

//
// Fake dma is a synchronous faking of the DMA progress used for
// isolating performance in the renderer.  The fakedma_updates is
// number of times S_Update() is called per second.
//

extern qboolean fakedma;
extern int fakedma_updates;
extern int paintedtime;
extern vec3 listener_origin;
extern vec3 listener_forward;
extern vec3 listener_right;
extern vec3 listener_up;
extern volatile dma_t *shm;
extern volatile dma_t sn;
extern vec_t sound_nominal_clip_dist;

extern cvar_t loadas8bit;
extern cvar_t bgmvolume;
extern cvar_t volume;

extern qboolean snd_initialized;

extern int snd_blocked;

void S_LocalSound(std::string_view s);

sfxcache_t *S_LoadSound(sfx_t *s);

wavinfo_t GetWavinfo(char *name, byte *wav, int wavlength);

void SNDDMA_Submit();

void S_AmbientOff();

void S_AmbientOn();

#endif
