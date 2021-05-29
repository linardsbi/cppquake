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
// Z_zone.c

#include "quakedef.hpp"


void Cache_FreeLow(int new_low_hunk);

void Cache_FreeHigh(int new_high_hunk);


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

memzone_t *mainzone;

void Z_ClearZone(memzone_t *zone, int size);


/*
========================
Z_ClearZone
========================
*/
void Z_ClearZone(memzone_t *zone, int size) {
    memblock_t *block = nullptr;

// set the entire zone to one free block

    zone->blocklist.next = zone->blocklist.prev = block =
            (memblock_t *) ((byte *) zone + sizeof(memzone_t));
    zone->blocklist.tag = 1;    // in use block
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0;            // free block
    block->id = ZONEID;
    block->size = size - sizeof(memzone_t);
}


/*
========================
Z_Free
========================
*/
void Z_Free(void *ptr) {
    memblock_t *block = nullptr, *other = nullptr;

    if (!ptr)
        Sys_Error("Z_Free: NULL pointer");

    block = (memblock_t *) ((byte *) ptr - sizeof(memblock_t));
    if (block->id != ZONEID)
        Sys_Error("Z_Free: freed a pointer without ZONEID");
    if (block->tag == 0)
        Sys_Error("Z_Free: freed a freed pointer");

    block->tag = 0;        // mark as free

    other = block->prev;
    if (!other->tag) {    // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;
        if (block == mainzone->rover)
            mainzone->rover = other;
        block = other;
    }

    other = block->next;
    if (!other->tag) {    // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;
        if (other == mainzone->rover)
            mainzone->rover = block;
    }
}


/*
========================
Z_Print
========================
*/
void Z_Print(memzone_t *zone) {
    memblock_t *block = nullptr;

    Con_Printf("zone size: %i  location: %p\n", mainzone->size, mainzone);

    for (block = zone->blocklist.next;; block = block->next) {
        Con_Printf("block:%p    size:%7i    tag:%3i\n",
                   block, block->size, block->tag);

        if (block->next == &zone->blocklist)
            break;            // all blocks have been hit
        if ((byte *) block + block->size != (byte *) block->next)
            Con_Printf("ERROR: block size does not touch the next block\n");
        if (block->next->prev != block)
            Con_Printf("ERROR: next block doesn't have proper back link\n");
        if (!block->tag && !block->next->tag)
            Con_Printf("ERROR: two consecutive free blocks\n");
    }
}


/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap() {
    memblock_t *block = nullptr;

    for (block = mainzone->blocklist.next;; block = block->next) {
        if (block->next == &mainzone->blocklist)
            break;            // all blocks have been hit
        if ((byte *) block + block->size != (byte *) block->next)
            Sys_Error("Z_CheckHeap: block size does not touch the next block\n");
        if (block->next->prev != block)
            Sys_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        if (!block->tag && !block->next->tag)
            Sys_Error("Z_CheckHeap: two consecutive free blocks\n");
    }
}

//============================================================================



byte *hunk_base;
long hunk_size;

long hunk_low_used;
long hunk_high_used;

qboolean hunk_tempactive;
int hunk_tempmark;

void R_FreeTextures();


/*
==============
Hunk_Check

Run consistancy and sentinal trahing checks
==============
*/
void Hunk_Check() {
    for (auto h = reinterpret_cast<hunk_t *>(hunk_base); (byte *) h != hunk_base + hunk_low_used;) {
        if (h->sentinal != HUNK_SENTINAL)
            Sys_Error("Hunk_Check: trahsed sentinal");
        if (h->size < 16 || h->size + (byte *) h - hunk_base > hunk_size)
            Sys_Error("Hunk_Check: bad size");
        h = (hunk_t *) ((byte *) h + h->size);
    }
}

/*
==============
Hunk_Print

If "all" is specified, every single allocation is printed.
Otherwise, allocations with the same name will be totaled up before printing.
==============
*/
void Hunk_Print(qboolean all) {
    hunk_t *h = nullptr, *next = nullptr, *endlow = nullptr, *starthigh = nullptr, *endhigh = nullptr;
    int count = 0, sum = 0;
    int totalblocks = 0;
    char name[9];

    name[8] = 0;
    count = 0;
    sum = 0;
    totalblocks = 0;

    h = (hunk_t *) hunk_base;
    endlow = (hunk_t *) (hunk_base + hunk_low_used);
    starthigh = (hunk_t *) (hunk_base + hunk_size - hunk_high_used);
    endhigh = (hunk_t *) (hunk_base + hunk_size);

    Con_Printf("          :%8i total hunk size\n", hunk_size);
    Con_Printf("-------------------------\n");

    while (true) {
        //
        // skip to the high hunk if done with low hunk
        //
        if (h == endlow) {
            Con_Printf("-------------------------\n");
            Con_Printf("          :%8i REMAINING\n", hunk_size - hunk_low_used - hunk_high_used);
            Con_Printf("-------------------------\n");
            h = starthigh;
        }

        //
        // if totally done, break
        //
        if (h == endhigh)
            break;

        //
        // run consistancy checks
        //
        if (h->sentinal != HUNK_SENTINAL)
            Sys_Error("Hunk_Check: trahsed sentinal");
        if (h->size < 16 || h->size + (byte *) h - hunk_base > hunk_size)
            Sys_Error("Hunk_Check: bad size");

        next = (hunk_t *) ((byte *) h + h->size);
        count++;
        totalblocks++;
        sum += h->size;

        //
        // print the single block
        //
        memcpy(name, h->name, 8);
        if (all)
            Con_Printf("%8p :%8i %8s\n", h, h->size, name);

        //
        // print the total
        //
        if (next == endlow || next == endhigh ||
            strncmp(h->name, next->name, 8) != 0) {
            if (!all)
                Con_Printf("          :%8i %8s (TOTAL)\n", sum, name);
            count = 0;
            sum = 0;
        }

        h = next;
    }

    Con_Printf("-------------------------\n");
    Con_Printf("%8i total blocks\n", totalblocks);

}

auto Hunk_LowMark() -> int {
    return hunk_low_used;
}

void Hunk_FreeToLowMark(int mark) {
    if (mark < 0 || mark > hunk_low_used)
        Sys_Error("Hunk_FreeToLowMark: bad mark %i", mark);
    memset(hunk_base + mark, 0, hunk_low_used - mark);
    hunk_low_used = mark;
}

auto Hunk_HighMark() -> int {
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }

    return hunk_high_used;
}

void Hunk_FreeToHighMark(int mark) {
    if (hunk_tempactive) {
        hunk_tempactive = false;
        Hunk_FreeToHighMark(hunk_tempmark);
    }
    if (mark < 0 || mark > hunk_high_used)
        Sys_Error("Hunk_FreeToHighMark: bad mark %i", mark);
    memset(hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
    hunk_high_used = mark;
}


/*
===============================================================================

CACHE MEMORY

===============================================================================
*/


cache_system_t cache_head;

/*
===========
Cache_Move
===========
*/
void Cache_Move(cache_system_t *c) {
// we are clearing up space at the bottom, so only allocate it late
    cache_system_t *newCache = Cache_TryAlloc(c->size, true);
    if (newCache != nullptr) {
//		Con_Printf ("cache_move ok\n");

        Q_memcpy(newCache + 1, c + 1, c->size - sizeof(cache_system_t));
        newCache->user = c->user;
        Q_memcpy(newCache->name, c->name, sizeof(newCache->name));
        Cache_Free(c->user);
        newCache->user->data = (void *) (newCache + 1);
    } else {
//		Con_Printf ("cache_move failed\n");

        Cache_Free(c->user);        // tough luck...
    }
}

/*
============
Cache_FreeLow

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeLow(long new_low_hunk) {
    cache_system_t *c = nullptr;

    while (true) {
        c = cache_head.next;
        if (c == &cache_head)
            return;        // nothing in cache at all
        if ((byte *) c >= hunk_base + new_low_hunk)
            return;        // there is space to grow the hunk
        Cache_Move(c);    // reclaim the space
    }
}

/*
============
Cache_FreeHigh

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeHigh(long new_high_hunk) {
    cache_system_t *c = nullptr, *prev = nullptr;

    prev = nullptr;
    while (true) {
        c = cache_head.prev;
        if (c == &cache_head)
            return;        // nothing in cache at all
        if ((byte *) c + c->size <= hunk_base + hunk_size - new_high_hunk)
            return;        // there is space to grow the hunk
        if (c == prev)
            Cache_Free(c->user);    // didn't move out of the way
        else {
            Cache_Move(c);    // try to move it
            prev = c;
        }
    }
}

void Cache_UnlinkLRU(cache_system_t *cs) {
    if (!cs->lru_next || !cs->lru_prev)
        Sys_Error("Cache_UnlinkLRU: NULL link");

    cs->lru_next->lru_prev = cs->lru_prev;
    cs->lru_prev->lru_next = cs->lru_next;

    cs->lru_prev = cs->lru_next = nullptr;
}

void Cache_MakeLRU(cache_system_t *cs) {
    if (cs->lru_next || cs->lru_prev)
        Sys_Error("Cache_MakeLRU: active link");

    cache_head.lru_next->lru_prev = cs;
    cs->lru_next = cache_head.lru_next;
    cs->lru_prev = &cache_head;
    cache_head.lru_next = cs;
}


/*
============
Cache_TryAlloc

Looks for a free block of memory between the high and low hunk marks
Size should already include the header and padding
============
*/
auto Cache_TryAlloc(int size, qboolean nobottom) -> cache_system_t * {
    cache_system_t *cs = nullptr, *newCache = nullptr;

// is the cache completely empty?

    if (!nobottom && cache_head.prev == &cache_head) {
        if (hunk_size - hunk_high_used - hunk_low_used < size)
            Sys_Error("Cache_TryAlloc: %i is greater then free hunk", size);

        newCache = (cache_system_t *) (hunk_base + hunk_low_used);
        memset(newCache, 0, sizeof(*newCache));
        newCache->size = size;

        cache_head.prev = cache_head.next = newCache;
        newCache->prev = newCache->next = &cache_head;

        Cache_MakeLRU(newCache);
        return newCache;
    }

// search from the bottom up for space

    newCache = (cache_system_t *) (hunk_base + hunk_low_used);
    cs = cache_head.next;

    do {
        if (!nobottom || cs != cache_head.next) {
            if ((byte *) cs - (byte *) newCache >= size) {    // found space
                memset(newCache, 0, sizeof(*newCache));
                newCache->size = size;

                newCache->next = cs;
                newCache->prev = cs->prev;
                cs->prev->next = newCache;
                cs->prev = newCache;

                Cache_MakeLRU(newCache);

                return newCache;
            }
        }

        // continue looking
        newCache = (cache_system_t *) ((byte *) cs + cs->size);
        cs = cs->next;

    } while (cs != &cache_head);

// try to allocate one at the very end
    if (hunk_base + hunk_size - hunk_high_used - (byte *) newCache >= size) {
        memset(newCache, 0, sizeof(*newCache));
        newCache->size = size;

        newCache->next = &cache_head;
        newCache->prev = cache_head.prev;
        cache_head.prev->next = newCache;
        cache_head.prev = newCache;

        Cache_MakeLRU(newCache);

        return newCache;
    }

    return nullptr;        // couldn't allocate
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush() {
    while (cache_head.next != &cache_head)
        Cache_Free(cache_head.next->user);    // reclaim the space
}


/*
============
Cache_Print

============
*/
void Cache_Print() {
    cache_system_t *cd = nullptr;

    for (cd = cache_head.next; cd != &cache_head; cd = cd->next) {
        Con_Printf("%8i : %s\n", cd->size, cd->name);
    }
}

/*
============
Cache_Report

============
*/
void Cache_Report() {
    Con_DPrintf("%4.1f megabyte data cache\n",
                static_cast<float>(hunk_size - hunk_high_used - hunk_low_used) / (float) (1024 * 1024));
}

/*
============
Cache_Init

============
*/
void Cache_Init() {
    cache_head.next = cache_head.prev = &cache_head;
    cache_head.lru_next = cache_head.lru_prev = &cache_head;

    Cmd_AddCommand("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free(cache_user_t *c) {
    cache_system_t *cs = nullptr;

    if (!c->data)
        Sys_Error("Cache_Free: not allocated");

    cs = ((cache_system_t *) c->data) - 1;

    cs->prev->next = cs->next;
    cs->next->prev = cs->prev;
    cs->next = cs->prev = nullptr;

    c->data = nullptr;

    Cache_UnlinkLRU(cs);
}


/*
==============
Cache_Check
==============
*/
auto Cache_Check(cache_user_t *c) -> void * {
    cache_system_t *cs = nullptr;

    if (!c->data)
        return nullptr;

    cs = ((cache_system_t *) c->data) - 1;

// move to head of LRU
    Cache_UnlinkLRU(cs);
    Cache_MakeLRU(cs);

    return c->data;
}

//============================================================================


/*
========================
Memory_Init
========================
*/
void Memory_Init(void *buf, int size) {
    int zonesize = DYNAMIC_SIZE;

    hunk_base = static_cast<byte *>(buf);
    hunk_size = size;
    hunk_low_used = 0;
    hunk_high_used = 0;

    Cache_Init();
    int p = COM_CheckParm("-zone");
    if (p) {
        if (p < com_argc - 1)
            zonesize = static_cast<int>(strtol(com_argv[p + 1], nullptr, 10)) * 1024;
        else
            Sys_Error("Memory_Init: you must specify a size in KB after -zone");
    }
    mainzone = reinterpret_cast<memzone_t *>(hunkAllocName<void *>(zonesize, "zone"));
    Z_ClearZone(mainzone, zonesize);
}

