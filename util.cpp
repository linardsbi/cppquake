#include "util.hpp"
#include "common.h"



template <typename MemType> MemType hunkAlloc(const int size) {
    return hunkAllocName<MemType>(size, "unknown");
}

template <typename MemType> MemType hunkAllocName(int size, char *name) {
#ifdef PARANOID
  Hunk_Check();
#endif

  if (size < 0)
    Sys_Error("Hunk_Alloc: bad size: %i", size);

  size = sizeof(hunk_t) + ((size + 15) & ~15);

  if (hunk_size - hunk_low_used - hunk_high_used < size)
    Sys_Error("Hunk_Alloc: failed on %i bytes", size);

  hunk_t *h = (hunk_t *)(hunk_base + hunk_low_used);
  hunk_low_used += size;

  Cache_FreeLow(hunk_low_used);

  memset(h, 0, size);

  h->size = size;
  h->sentinal = HUNK_SENTINAL;
  Q_strncpy(h->name, name, 8);

  return static_cast<MemType>(h + 1);
}

template <typename MemType> MemType zmalloc(int size) {
  Z_CheckHeap(); // DEBUG
  MemType buf = tagMalloc(size, 1);
  if (buf == nullptr)
    Sys_Error("zMalloc: failed on allocation of %i bytes", size);
  Q_memset(buf, 0, size);

  return buf;
}
template <typename MemType> 
MemType tagMalloc (int size, int tag)
{
	memblock_t	*start, *rover, *newB, *base;

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
			return NULL;
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

	return static_cast<MemType>((byte *)base + sizeof(memblock_t));
}

template <typename MemType>
MemType SZGetSpace (sizebuf_t *buf, int length)
{
	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");
		
		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);
			
		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf); 
	}

	MemType data = buf->data + buf->cursize;
	buf->cursize += length;
	
	return data;
}

template <typename MemType>
MemType hunkHighAllocName (int size, char *name) {
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

	hunk_t *h = (hunk_t *)(hunk_base + hunk_size - hunk_high_used);

	memset (h, 0, size);
	h->size = size;
	h->sentinal = HUNK_SENTINAL;
	Q_strncpy (h->name, name, 8);

	return static_cast<MemType>(h+1);
}

template <typename MemType>
MemType hunkTempAlloc(int size) {
	size = (size+15)&~15;
	
	if (hunk_tempactive)
	{
		Hunk_FreeToHighMark (hunk_tempmark);
		hunk_tempactive = false;
	}
	
	hunk_tempmark = Hunk_HighMark ();

	MemType buf = Hunk_HighAllocName (size, "temp");

	hunk_tempactive = true;

	return buf;
}

template <typename MemType>
MemType cacheCheck (cache_user_t *c)
{
	if (!c->data)
		return NULL;

	cache_system_t	*cs = ((cache_system_t *)c->data) - 1;

// move to head of LRU
	Cache_UnlinkLRU (cs);
	Cache_MakeLRU (cs);
	
	return static_cast<MemType>(c->data);
}

template <typename MemType>
MemType cacheAlloc (cache_user_t *c, int size, char *name)
{
	if (c->data)
		Sys_Error ("Cache_Alloc: allready allocated");
	
	if (size <= 0)
		Sys_Error ("Cache_Alloc: size %i", size);

	size = (size + sizeof(cache_system_t) + 15) & ~15;

	cache_system_t	*cs;
// find memory for it	
	while (1)
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