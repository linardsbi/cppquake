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
// snd_dma.c -- main control for any streaming sound output device


#include <string>
#include <cmath>
#include "quakedef.hpp"

#include "sound.hpp"

#ifdef _WIN32
#include ""winquake.hpp""
#endif

void S_Play();

void S_PlayVol();

void S_SoundList();

void S_Update_();

void S_StopAllSounds(qboolean clear);

void S_StopAllSoundsC();

// =======================================================================
// Internal sound data & structures
// =======================================================================

channel_t channels[MAX_CHANNELS];
int total_channels;

int snd_blocked = 0;
static qboolean snd_ambient = true;
qboolean snd_initialized = false;

// pointer should go away
volatile dma_t *shm = nullptr;
volatile dma_t sn;

vec3 listener_origin;
vec3 listener_forward;
vec3 listener_right;
vec3 listener_up;
vec_t sound_nominal_clip_dist = 1000.0;

int soundtime;        // sample PAIRS
int paintedtime;    // sample PAIRS


#define    MAX_SFX        512
sfx_t *known_sfx;        // hunk allocated [MAX_SFX]
int num_sfx;

sfx_t *ambient_sfx[NUM_AMBIENTS];

int sound_started = 0;

cvar_t bgmvolume = {"bgmvolume", "1", true};
cvar_t volume = {"volume", "0.7", true};

cvar_t nosound = {"nosound", "0"};
cvar_t precache = {"precache", "1"};
cvar_t loadas8bit = {"loadas8bit", "0"};
cvar_t bgmbuffer = {"bgmbuffer", "4096"};
cvar_t ambient_level = {"ambient_level", "0.3"};
cvar_t ambient_fade = {"ambient_fade", "100"};
cvar_t snd_noextraupdate = {"snd_noextraupdate", "0"};
cvar_t snd_show = {"snd_show", "0"};
cvar_t _snd_mixahead = {"_snd_mixahead", "0.1", true};


// ====================================================================
// User-setable variables
// ====================================================================


//
// Fake dma is a synchronous faking of the DMA progress used for
// isolating performance in the renderer.  The fakedma_updates is
// number of times S_Update() is called per second.
//

qboolean fakedma = false;
int fakedma_updates = 15;


void S_AmbientOff() {
    snd_ambient = false;
}


void S_AmbientOn() {
    snd_ambient = true;
}


void S_SoundInfo_f() {
    if (!sound_started || !shm) {
        Con_Printf("sound system not started\n");
        return;
    }

    Con_Printf("%5d stereo\n", shm->channels - 1);
    Con_Printf("%5d samples\n", shm->samples);
    Con_Printf("%5d samplepos\n", shm->samplepos);
    Con_Printf("%5d samplebits\n", shm->samplebits);
    Con_Printf("%5d submission_chunk\n", shm->submission_chunk);
    Con_Printf("%5d speed\n", shm->speed);
    Con_Printf("%p dma buffer\n", (void*)shm->buffer);
    Con_Printf("%5d total_channels\n", total_channels);
}


/*
================
S_Startup
================
*/

void S_Startup() {
    int rc = 0;

    if (!snd_initialized)
        return;

    if (!fakedma) {
        rc = SNDDMA_Init();

        if (!rc) {
#ifndef    _WIN32
            Con_Printf("S_Startup: SNDDMA_Init failed.\n");
#endif
            sound_started = 0;
            return;
        }
    }

    sound_started = 1;
}


/*
================
S_Init
================
*/
void S_Init() {

    Con_Printf("\nSound Initialization\n");

    if (COM_CheckParm("-nosound"))
        return;

    if (COM_CheckParm("-simsound"))
        fakedma = true;

    Cmd_AddCommand("play", S_Play);
    Cmd_AddCommand("playvol", S_PlayVol);
    Cmd_AddCommand("stopsound", S_StopAllSoundsC);
    Cmd_AddCommand("soundlist", S_SoundList);
    Cmd_AddCommand("soundinfo", S_SoundInfo_f);

    Cvar_RegisterVariable(&nosound);
    Cvar_RegisterVariable(&volume);
    Cvar_RegisterVariable(&precache);
    Cvar_RegisterVariable(&loadas8bit);
    Cvar_RegisterVariable(&bgmvolume);
    Cvar_RegisterVariable(&bgmbuffer);
    Cvar_RegisterVariable(&ambient_level);
    Cvar_RegisterVariable(&ambient_fade);
    Cvar_RegisterVariable(&snd_noextraupdate);
    Cvar_RegisterVariable(&snd_show);
    Cvar_RegisterVariable(&_snd_mixahead);

    if (host_parms.memsize < 0x800000) {
        Cvar_Set("loadas8bit", "1");
        Con_Printf("loading all sounds as 8bit\n");
    }


    snd_initialized = true;

    S_Startup();

    known_sfx = hunkAllocName<decltype(known_sfx)>(MAX_SFX * sizeof(sfx_t), "sfx_t");
    num_sfx = 0;

// create a piece of DMA memory

    if (fakedma) {
        shm = hunkAllocName<decltype(shm)>(sizeof(*shm), "shm");
        shm->splitbuffer = false;
        shm->samplebits = 16;
        shm->speed = 22050;
        shm->channels = 2;
        shm->samples = 32768;
        shm->samplepos = 0;
        shm->soundalive = true;
        shm->gamealive = true;
        shm->submission_chunk = 1;
        shm->buffer = hunkAllocName<decltype(shm->buffer)>(1 << 16, "shmbuf");
    }

    if (shm) {
        Con_Printf("Sound sampling rate: %i\n", shm->speed);
    }

    // provides a tick sound until washed clean

//	if (shm->buffer)
//		shm->buffer[4] = shm->buffer[5] = 0x7f;	// force a pop for debugging

    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");

    S_StopAllSounds(true);
}


// =======================================================================
// Shutdown sound engine
// =======================================================================

void S_Shutdown() {

    if (!sound_started)
        return;

    if (shm)
        shm->gamealive = false;

    shm = nullptr;
    sound_started = 0;

    if (!fakedma) {
        SNDDMA_Shutdown();
    }
}


// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_FindName

==================
*/
auto S_FindName(std::string_view name) -> sfx_t * {
    if (name.empty())
        Sys_Error("S_FindName: NULL\n");

    if (name.length() >= MAX_QPATH)
        Sys_Error("Sound name too long: %s", name);

    int i = 0;
// see if already loaded
    for (i = 0; i < num_sfx; i++)
        if (!Q_strcmp(known_sfx[i].name, name)) {
            return &known_sfx[i];
        }

    if (num_sfx == MAX_SFX)
        Sys_Error("S_FindName: out of sfx_t");

    auto sfx = &known_sfx[i];
    strncpy(sfx->name, name.data(), name.length());

    num_sfx++;

    return sfx;
}


/*
==================
S_TouchSound

==================
*/
void S_TouchSound(char *name) {
    sfx_t *sfx = nullptr;

    if (!sound_started)
        return;

    sfx = S_FindName(name);
    Cache_Check(&sfx->cache);
}

/*
==================
S_PrecacheSound

==================
*/
auto S_PrecacheSound(std::string_view name) -> sfx_t * {
    if (!sound_started || nosound.value == 1)
        return nullptr;

    sfx_t *sfx = S_FindName(name);

// cache it in
    if (precache.value == 0)
        S_LoadSound(sfx);

    return sfx;
}


//=============================================================================

/*
=================
SND_PickChannel
=================
*/
auto SND_PickChannel(int entnum, int entchannel) -> channel_t * {
    int ch_idx = 0;
    int first_to_die = 0;
    int life_left = 0;

// Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = 0x7fffffff;
    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++) {
        if (entchannel != 0        // channel 0 never overrides
            && channels[ch_idx].entnum == entnum
            && (channels[ch_idx].entchannel == entchannel ||
                entchannel == -1)) {    // allways override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let monster sounds override player sounds
        if (channels[ch_idx].entnum == cl.viewentity && entnum != cl.viewentity && channels[ch_idx].sfx)
            continue;

        if (channels[ch_idx].end - paintedtime < life_left) {
            life_left = channels[ch_idx].end - paintedtime;
            first_to_die = ch_idx;
        }
    }

    if (first_to_die == -1)
        return nullptr;

    if (channels[first_to_die].sfx)
        channels[first_to_die].sfx = nullptr;

    return &channels[first_to_die];
}

/*
=================
SND_Spatialize
=================
*/
void SND_Spatialize(channel_t *ch) {
    vec_t dot = NAN;
    vec_t ldist = NAN, rdist = NAN, dist = NAN;
    vec_t lscale = NAN, rscale = NAN, scale = NAN;
    sfx_t *snd = nullptr;

// anything coming from the view entity will allways be full volume
    if (ch->entnum == cl.viewentity) {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;
        return;
    }

// calculate stereo seperation and distance attenuation

    snd = ch->sfx;
    auto source_vec = ch->origin - listener_origin;

    dist = VectorNormalize(source_vec) * ch->dist_mult;

    dot = glm::dot(listener_right, source_vec);

    if (shm->channels == 1) {
        rscale = 1.0;
        lscale = 1.0;
    } else {
        rscale = 1.0 + dot;
        lscale = 1.0 - dot;
    }

// add in distance effect
    scale = (1.0 - dist) * rscale;
    ch->rightvol = (int) (ch->master_vol * scale);
    if (ch->rightvol < 0)
        ch->rightvol = 0;

    scale = (1.0 - dist) * lscale;
    ch->leftvol = (int) (ch->master_vol * scale);
    if (ch->leftvol < 0)
        ch->leftvol = 0;
}


// =======================================================================
// Start a sound effect
// =======================================================================

void S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3 origin, float fvol, float attenuation) {
    channel_t *target_chan = nullptr, *check = nullptr;
    sfxcache_t *sc = nullptr;
    int vol = 0;
    int ch_idx = 0;
    int skip = 0;

    if (!sound_started)
        return;

    if (!sfx)
        return;

    if (nosound.value)
        return;

    vol = fvol * 255;

// pick a channel to play on
    target_chan = SND_PickChannel(entnum, entchannel);
    if (!target_chan)
        return;

// spatialize
    memset(target_chan, 0, sizeof(*target_chan));
    target_chan->origin = origin;
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = vol;
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    SND_Spatialize(target_chan);

    if (!target_chan->leftvol && !target_chan->rightvol)
        return;        // not audible at all

// new channel
    sc = S_LoadSound(sfx);
    if (!sc) {
        target_chan->sfx = nullptr;
        return;        // couldn't load the sound's data
    }

    target_chan->sfx = sfx;
    target_chan->pos = 0.0;
    target_chan->end = paintedtime + sc->length;

// if an identical sound has also been started this frame, offset the pos
// a bit to keep it from just making the first one louder
    check = &channels[NUM_AMBIENTS];
    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; ch_idx++, check++) {
        if (check == target_chan)
            continue;
        if (check->sfx == sfx && !check->pos) {
            skip = rand() % (int) (0.1 * shm->speed);
            if (skip >= target_chan->end)
                skip = target_chan->end - 1;
            target_chan->pos += skip;
            target_chan->end -= skip;
            break;
        }

    }
}

void S_StopSound(int entnum, int entchannel) {
    int i = 0;

    for (i = 0; i < MAX_DYNAMIC_CHANNELS; i++) {
        if (channels[i].entnum == entnum
            && channels[i].entchannel == entchannel) {
            channels[i].end = 0;
            channels[i].sfx = nullptr;
            return;
        }
    }
}

void S_StopAllSounds(qboolean clear) {
    int i = 0;

    if (!sound_started)
        return;

    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;    // no statics

    for (i = 0; i < MAX_CHANNELS; i++)
        if (channels[i].sfx)
            channels[i].sfx = nullptr;

    Q_memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));

    if (clear)
        S_ClearBuffer();
}

void S_StopAllSoundsC() {
    S_StopAllSounds(true);
}

void S_ClearBuffer() {
    int clear = 0;

#ifdef _WIN32
    if (!sound_started || !shm || (!shm->buffer && !pDSBuf))
#else
    if (!sound_started || !shm || !shm->buffer)
#endif
        return;

    if (shm->samplebits == 8)
        clear = 0x80;
    else
        clear = 0;

#ifdef _WIN32
    if (pDSBuf)
    {
        DWORD	dwSize;
        DWORD	*pData;
        int		reps;
        HRESULT	hresult;

        reps = 0;

        while ((hresult = pDSBuf->lpVtbl->Lock(pDSBuf, 0, gSndBufSize, &pData, &dwSize, NULL, NULL, 0)) != DS_OK)
        {
            if (hresult != DSERR_BUFFERLOST)
            {
                Con_Printf ("S_ClearBuffer: DS::Lock Sound Buffer Failed\n");
                S_Shutdown ();
                return;
            }

            if (++reps > 10000)
            {
                Con_Printf ("S_ClearBuffer: DS: couldn't restore buffer\n");
                S_Shutdown ();
                return;
            }
        }

        Q_memset(pData, clear, shm->samples * shm->samplebits/8);

        pDSBuf->lpVtbl->Unlock(pDSBuf, pData, dwSize, NULL, 0);

    }
    else
#endif
    {
        Q_memset(shm->buffer, clear, shm->samples * shm->samplebits / 8);
    }
}


/*
=================
S_StaticSound
=================
*/
void S_StaticSound(sfx_t *sfx, vec3 origin, float vol, float attenuation) {
    channel_t *ss = nullptr;
    sfxcache_t *sc = nullptr;

    if (!sfx)
        return;

    if (total_channels == MAX_CHANNELS) {
        Con_Printf("total_channels == MAX_CHANNELS\n");
        return;
    }

    ss = &channels[total_channels];
    total_channels++;

    sc = S_LoadSound(sfx);
    if (!sc)
        return;

    if (sc->loopstart == -1) {
        Con_Printf("Sound %s not looped\n", sfx->name);
        return;
    }

    ss->sfx = sfx;
    ss->origin = origin;
    ss->master_vol = vol;
    ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sc->length;

    SND_Spatialize(ss);
}


//=============================================================================

/*
===================
S_UpdateAmbientSounds
===================
*/
void S_UpdateAmbientSounds() {
    mleaf_t *l = nullptr;
    float vol = NAN;
    int ambient_channel = 0;
    channel_t *chan = nullptr;

    if (!snd_ambient)
        return;

// calc ambient sound levels
    if (!cl.worldmodel)
        return;

    l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    if (!l || !ambient_level.value) {
        for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++)
            channels[ambient_channel].sfx = nullptr;
        return;
    }

    for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS; ambient_channel++) {
        chan = &channels[ambient_channel];
        chan->sfx = ambient_sfx[ambient_channel];

        vol = ambient_level.value * l->ambient_sound_level[ambient_channel];
        if (vol < 8)
            vol = 0;

        // don't adjust volume too fast
        if (chan->master_vol < vol) {
            chan->master_vol += host_frametime * ambient_fade.value;
            if (chan->master_vol > vol)
                chan->master_vol = vol;
        } else if (chan->master_vol > vol) {
            chan->master_vol -= host_frametime * ambient_fade.value;
            if (chan->master_vol < vol)
                chan->master_vol = vol;
        }

        chan->leftvol = chan->rightvol = chan->master_vol;
    }
}


/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update(vec3 origin, vec3 forward, vec3 right, vec3 up) {
    int i = 0, j = 0;
    int total = 0;
    channel_t *ch = nullptr;
    channel_t *combine = nullptr;

    if (!sound_started || (snd_blocked > 0))
        return;

    listener_origin = origin;
    listener_forward = forward;
    listener_right = right;
    listener_up = up;

// update general area ambient sound sources
    S_UpdateAmbientSounds();

    combine = nullptr;

// update spatialization for static and dynamic sounds	
    ch = channels + NUM_AMBIENTS;
    for (i = NUM_AMBIENTS; i < total_channels; i++, ch++) {
        if (!ch->sfx)
            continue;
        SND_Spatialize(ch);         // respatialize channel
        if (!ch->leftvol && !ch->rightvol)
            continue;

        // try to combine static sounds with a previous channel of the same
        // sound effect so we don't mix five torches every frame

        if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS) {
            // see if it can just use the last one
            if (combine && combine->sfx == ch->sfx) {
                combine->leftvol += ch->leftvol;
                combine->rightvol += ch->rightvol;
                ch->leftvol = ch->rightvol = 0;
                continue;
            }
            // search for one
            combine = channels + MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
            for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i; j++, combine++)
                if (combine->sfx == ch->sfx)
                    break;

            if (j == total_channels) {
                combine = nullptr;
            } else {
                if (combine != ch) {
                    combine->leftvol += ch->leftvol;
                    combine->rightvol += ch->rightvol;
                    ch->leftvol = ch->rightvol = 0;
                }
                continue;
            }
        }


    }

//
// debugging output
//
    if (snd_show.value) {
        total = 0;
        ch = channels;
        for (i = 0; i < total_channels; i++, ch++)
            if (ch->sfx && (ch->leftvol || ch->rightvol)) {
                //Con_Printf ("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
                total++;
            }

        Con_Printf("----(%i)----\n", total);
    }

// mix some sound
    S_Update_();
}

void GetSoundtime() {
    int samplepos = 0;
    static int buffers;
    static int oldsamplepos;
    int fullsamples = 0;

    fullsamples = shm->samples / shm->channels;

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
#ifdef __sun__
    soundtime = SNDDMA_GetSamples();
#else
    samplepos = SNDDMA_GetDMAPos();


    if (samplepos < oldsamplepos) {
        buffers++;                    // buffer wrapped

        if (paintedtime > 0x40000000) {    // time to chop things off to avoid 32 bit limits
            buffers = 0;
            paintedtime = fullsamples;
            S_StopAllSounds(true);
        }
    }
    oldsamplepos = samplepos;

    soundtime = buffers * fullsamples + samplepos / shm->channels;
#endif
}

void S_ExtraUpdate() {

#ifdef _WIN32
    IN_Accumulate ();
#endif

    if (snd_noextraupdate.value)
        return;        // don't pollute timings
    S_Update_();
}

void S_Update_() {
#ifndef SDL

    unsigned        endtime;
    int				samps;

    if (!sound_started || (snd_blocked > 0))
        return;

// Updates DMA time
    GetSoundtime();

// check to make sure that we haven't overshot
    if (paintedtime < soundtime)
    {
        //Con_Printf ("S_Update_ : overflow\n");
        paintedtime = soundtime;
    }

// mix ahead of current position
    endtime = soundtime + _snd_mixahead.value * shm->speed;
    samps = shm->samples >> (shm->channels-1);
    if (endtime - soundtime > samps)
        endtime = soundtime + samps;

#ifdef _WIN32
// if the buffer was lost or stopped, restore it and/or restart it
    {
        DWORD	dwStatus;

        if (pDSBuf)
        {
            if (pDSBuf->lpVtbl->GetStatus (pDSBuf, &dwStatus) != DD_OK)
                Con_Printf ("Couldn't get sound buffer status\n");

            if (dwStatus & DSBSTATUS_BUFFERLOST)
                pDSBuf->lpVtbl->Restore (pDSBuf);

            if (!(dwStatus & DSBSTATUS_PLAYING))
                pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);
        }
    }
#endif

    S_PaintChannels (endtime);

    SNDDMA_Submit ();
#endif /* ! SDL */
}

/*
===============================================================================

console functions

===============================================================================
*/

void S_Play() {
    static int hash = 345;
    int i = 0;
    sfx_t *sfx = nullptr;
    std::string name;
    i = 1;
    while (i < Cmd_Argc()) {
        name = Cmd_Argv(i);
        if (name.find('.') == std::string::npos) {
            name += ".wav";
        }

        sfx = S_PrecacheSound(name);
        S_StartSound(hash++, 0, sfx, listener_origin, 1.0, 1.0);
        i++;
    }
}

void S_PlayVol() {
    static int hash = 543;
    int i = 1;

    while (i < Cmd_Argc()) {
        std::string name(Cmd_Argv(i));
        if (name.find('.') == std::string::npos) {
            name += ".wav";
        }

        sfx_t *sfx = S_PrecacheSound(name);
        float vol = Q_atof(Cmd_Argv(i + 1));
        S_StartSound(hash++, 0, sfx, listener_origin, vol, 1.0);
        i += 2;
    }
}

void S_SoundList() {
    int i = 0;
    sfx_t *sfx = nullptr;
    sfxcache_t *sc = nullptr;
    int size = 0, total = 0;

    total = 0;
    for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
        sc = cacheCheck<decltype(sc)>(&sfx->cache);
        if (!sc)
            continue;
        size = sc->length * sc->width * (sc->stereo + 1);
        total += size;
        if (sc->loopstart >= 0)
            Con_Printf("L");
        else
            Con_Printf(" ");
        Con_Printf("(%2db) %6i : %s\n", sc->width * 8, size, sfx->name);
    }
    Con_Printf("Total resident: %i\n", total);
}


void S_LocalSound(std::string_view sound) {
    if (nosound.value == 0)
        return;
    if (!sound_started)
        return;

    auto *sfx = S_PrecacheSound(sound);
    if (!sfx) {
        Con_Printf("S_LocalSound: can't cache %s\n", sound);
        return;
    }
    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
}


void S_ClearPrecache() {
}


void S_BeginPrecaching() {
}


void S_EndPrecaching() {
}

