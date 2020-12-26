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
/*
 memory allocation


H_??? The hunk manages the entire memory block given to quake.  It must be
contiguous.  Memory can be allocated from either the low or high end in a
stack fashion.  The only way memory is released is by resetting one of the
pointers.

Hunk allocations should be given a name, so the Hunk_Print () function
can display usage.

Hunk allocations are guaranteed to be 16 byte aligned.

The video buffers are allocated high to avoid leaving a hole underneath
server allocations when changing to a higher video mode.


Z_??? Zone memory functions used for small, dynamic allocations like text
strings from command input.  There is only about 48K for it, allocated at
the very bottom of the hunk.

Cache_??? Cache memory is for objects that can be dynamically loaded and
can usefully stay persistant between levels.  The size of the cache
fluctuates from level to level.

To allocate a cachable object


Temp_??? Temp memory is used for file loading and surface caching.  The size
of the cache memory is adjusted so that there is a minimum of 512k remaining
for temp memory.


------ Top of Memory -------

high hunk allocations

<--- high hunk reset point held by vid

video buffer

z buffer

surface cache

<--- high hunk used

cachable memory

<--- low hunk used

client and server low hunk allocations

<-- low hunk reset point held by host

startup hunk allocations

Zone block

----- Bottom of Memory -----



*/
#ifndef ZONE_HPP
#define ZONE_HPP

#include "console.hpp"
#include "sys.hpp"
#include "common.hpp"

#include <cstring>

using memblock_t = struct memblock_s
{
    int		size;           // including the header and possibly tiny fragments
    int     tag;            // a tag of 0 is a free block
    int     id;        		// should be ZONEID
    memblock_s       *next, *prev;
    int		pad;			// pad to 64 bit boundary; (sizeof(memblock_t) % 8 == 0)
};

struct memzone_t
{
    int		size;		// total bytes malloced, including header
    memblock_t	blocklist;		// start / end cap for linked list
    memblock_t	*rover;
};

using cache_user_t = struct cache_user_s
{
    void	*data;
};

using cache_system_t = struct cache_system_s
{
    int						size;		// including this header
    cache_user_t			*user;
    char					name[16];
    struct cache_system_s	*prev, *next;
    struct cache_system_s	*lru_prev, *lru_next;	// for LRU flushing
};

struct hunk_t
{
    int		sentinal;
    int		size;		// including sizeof(hunk_t), -1 = not allocated
    char	name[8];
};

extern memzone_t	*mainzone;

constexpr int HUNK_SENTINAL =	0x1df001ed;
constexpr int DYNAMIC_SIZE = 0xc000;

constexpr int ZONEID = 0x1d4a11;
constexpr int MINFRAGMENT =	64;

extern byte	*hunk_base;
extern long		hunk_size;

extern long		hunk_low_used;
extern long		hunk_high_used;

extern qboolean	hunk_tempactive;
extern int		hunk_tempmark;

extern cache_system_t	cache_head;

void Memory_Init (void *buf, int size);

void Z_Free (void *ptr);

void Z_CheckHeap ();

auto	Hunk_LowMark () -> int;
void Hunk_FreeToLowMark (int mark);

auto	Hunk_HighMark () -> int;
void Hunk_FreeToHighMark (int mark);

void Hunk_Check ();

void Cache_Flush ();

auto Cache_Check (cache_user_t *c) -> void *;
// returns the cached data, and moves to the head of the LRU list
// if present, otherwise returns NULL

void Cache_Free (cache_user_t *c);
void Cache_FreeLow (long new_low_hunk);
void Cache_FreeHigh (long new_high_hunk);

void Cache_UnlinkLRU (cache_system_t *cs);
void Cache_MakeLRU (cache_system_t *cs);

auto Cache_TryAlloc (int size, qboolean nobottom) -> cache_system_t *;


// Returns NULL if all purgable data was tossed and there still
// wasn't enough room.

void Cache_Report ();

template <typename MemType>
auto hunkAllocName(int hsize, const char *name) -> MemType {
#ifdef PARANOID
    Hunk_Check();
#endif

    if (hsize < 0)
        Sys_Error("Hunk_Alloc: bad size: %i", hsize);

    auto size = sizeof(hunk_t) + ((hsize + 0b111) & ~0b111);

    if (hunk_size - hunk_low_used - hunk_high_used < size)
        Sys_Error("Hunk_Alloc: failed on %i bytes", size);

    auto *h = (hunk_t *)(hunk_base + hunk_low_used);
    hunk_low_used += size;

    Cache_FreeLow(hunk_low_used);

    memset(h, 0, size);

    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    strncpy(h->name, name, 8);

    return reinterpret_cast<MemType>(h + 1);
}

template <typename MemType>
auto hunkAlloc (int size) -> MemType {
    return hunkAllocName<MemType>(size, "unknown");
}

template <typename MemType>
auto tagMalloc (int size, int tag) -> MemType
{
    memblock_t	*start = nullptr, *rover = nullptr, *newB = nullptr, *base = nullptr;

    if (!tag)
        Sys_Error ("TagMalloc: tried to use a 0 tag");

//
// scan through the block list looking for the first free block
// of sufficient size
//
    size += sizeof(memblock_t);	// account for size of block header
    size += 4;					// space for memory trash tester
    size = (size + 7) & ~7;		// align to 8-byte boundary

    base = rover = mainzone->rover;
    start = base->prev;

    do
    {
        if (rover == start)	// scaned all the way around the list
            return nullptr;
        if (rover->tag)
            base = rover = rover->next;
        else
            rover = rover->next;
    } while (base->tag || base->size < size);

//
// found a block big enough
//
    const int extra = base->size - size;
    if (extra >  MINFRAGMENT)
    {	// there will be a free fragment after the allocated block
        newB = (memblock_t *) ((byte *)base + size );
        newB->size = extra;
        newB->tag = 0;			// free block
        newB->prev = base;
        newB->id = ZONEID;
        newB->next = base->next;
        newB->next->prev = newB;
        base->next = newB;
        base->size = size;
    }

    base->tag = tag;				// no longer a free block

    mainzone->rover = base->next;	// next allocation will start looking here

    base->id = ZONEID;

// marker for memory trash testing
    *(int *)((byte *)base + base->size - 4) = ZONEID;

    return reinterpret_cast<MemType>((byte *)base + sizeof(memblock_t));
}

template <typename MemType> auto zmalloc(int size) -> MemType {
    Z_CheckHeap(); // DEBUG
    auto buf = tagMalloc<MemType>(size, 1);
    if (buf == nullptr)
        Sys_Error("zMalloc: failed on allocation of %i bytes", size);
    Q_memset(buf, 0, size);

    return buf;
}

template <typename MemType>
auto hunkHighAllocName (int size, char *name) -> MemType {
    if (size < 0)
        Sys_Error ("Hunk_HighAllocName: bad size: %i", size);

    if (hunk_tempactive)
    {
        Hunk_FreeToHighMark (hunk_tempmark);
        hunk_tempactive = false;
    }

#ifdef PARANOID
    Hunk_Check ();
#endif

    size = sizeof(hunk_t) + ((size+15)&~15);

    if (hunk_size - hunk_low_used - hunk_high_used < size)
    {
        Con_Printf ("Hunk_HighAlloc: failed on %i bytes\n",size);
        return NULL;
    }

    hunk_high_used += size;
    Cache_FreeHigh (hunk_high_used);

    auto *h = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);

    memset (h, 0, size);
    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy (h->name, name, 8);

    return reinterpret_cast<MemType>(h+1);
}

template <typename MemType>
auto hunkTempAlloc(int size) -> MemType {
    size = (size+15)&~15;

    if (hunk_tempactive)
    {
        Hunk_FreeToHighMark (hunk_tempmark);
        hunk_tempactive = false;
    }

    hunk_tempmark = Hunk_HighMark ();

    MemType buf = hunkHighAllocName<MemType>(size, "temp");

    hunk_tempactive = true;

    return buf;
}

template <typename MemType>
auto cacheCheck (cache_user_t *c) -> MemType
{
    if (!c->data)
        return nullptr;

    cache_system_t	*cs = ((cache_system_t *)c->data) - 1;

// move to head of LRU
    Cache_UnlinkLRU (cs);
    Cache_MakeLRU (cs);

    return static_cast<MemType>(c->data);
}

template <typename MemType>
auto cacheAlloc (cache_user_t *c, int size, char *name) -> MemType
{
    if (c->data)
        Sys_Error ("Cache_Alloc: allready allocated");

    if (size <= 0)
        Sys_Error ("Cache_Alloc: size %i", size);

    size = (size + sizeof(cache_system_t) + 15) & ~15;

    cache_system_t	*cs = nullptr;
// find memory for it
    while (true)
    {
        cs = Cache_TryAlloc (size, false);
        if (cs)
        {
            strncpy (cs->name, name, sizeof(cs->name)-1);
            c->data = (void *)(cs+1);
            cs->user = c;
            break;
        }

        // free the least recently used cahedat
        if (cache_head.lru_prev == &cache_head)
            Sys_Error ("Cache_Alloc: out of memory");
        // not enough memory at all
        Cache_Free ( cache_head.lru_prev->user );
    }

    return cacheCheck<MemType>(c);
}

#endif