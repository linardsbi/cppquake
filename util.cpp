#include "util.hpp"
#include "common.h"

using byte = unsigned char;

struct hunk_t {
  int sentinal;
  int size; // including sizeof(hunk_t), -1 = not allocated
  char name[8];
};

struct memblock_t
{
	int		size;           // including the header and possibly tiny fragments
	int     tag;            // a tag of 0 is a free block
	int     id;        		// should be ZONEID
	memblock_t       *next, *prev;
	int		pad;			// pad to 64 bit boundary
};

constexpr int HUNK_SENTINAL = 0x1df001ed;

byte	*hunk_base;
int		hunk_size;

int		hunk_low_used;
int		hunk_high_used;

qboolean	hunk_tempactive;
int		hunk_tempmark;

void Hunk_Check ()
{
	for (auto h = static_cast<hunk_t *>(hunk_base) ; (byte *)h != hunk_base + hunk_low_used ; )
	{
		if (h->sentinal != HUNK_SENTINAL)
			Sys_Error ("Hunk_Check: trahsed sentinal");
		if (h->size < 16 || h->size + (byte *)h - hunk_base > hunk_size)
			Sys_Error ("Hunk_Check: bad size");
		h = (hunk_t *)((byte *)h+h->size);
	}
}

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
  MemType *buf = Z_TagMalloc(size, 1);
  if (!buf)
    Sys_Error("Z_Malloc: failed on allocation of %i bytes", size);
  Q_memset(buf, 0, size);

  return buf;
}
template <typename MemType> 
MemType Z_TagMalloc (int size, int tag)
{
	memblock_t	*start, *rover, *newB, *base;

	if (!tag)
		Sys_Error ("Z_TagMalloc: tried to use a 0 tag");

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

	return (void *)(h+1);
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

void Hunk_FreeToHighMark (int mark)
{
	if (hunk_tempactive)
	{
		hunk_tempactive = false;
		Hunk_FreeToHighMark (hunk_tempmark);
	}
	if (mark < 0 || mark > hunk_high_used)
		Sys_Error ("Hunk_FreeToHighMark: bad mark %i", mark);
	memset (hunk_base + hunk_size - hunk_high_used, 0, hunk_high_used - mark);
	hunk_high_used = mark;
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

void Cache_Free (cache_user_t *c)
{
	cache_system_t	*cs;

	if (!c->data)
		Sys_Error ("Cache_Free: not allocated");

	cs = ((cache_system_t *)c->data) - 1;

	cs->prev->next = cs->next;
	cs->next->prev = cs->prev;
	cs->next = cs->prev = NULL;

	c->data = NULL;

	Cache_UnlinkLRU (cs);
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
	
	return Cache_Check (c);
}


cache_system_t *Cache_TryAlloc (int size, qboolean nobottom)
{
	cache_system_t	*cs, *newc;
	
// is the cache completely empty?

	if (!nobottom && cache_head.prev == &cache_head)
	{
		if (hunk_size - hunk_high_used - hunk_low_used < size)
			Sys_Error ("Cache_TryAlloc: %i is greater then free hunk", size);

		newc = (cache_system_t *) (hunk_base + hunk_low_used);
		memset (newc, 0, sizeof(*newc));
		newc->size = size;

		cache_head.prev = cache_head.next = newc;
		newc->prev = newc->next = &cache_head;
		
		Cache_MakeLRU (newc);
		return newc;
	}
	
// search from the bottom up for space

	newc = (cache_system_t *) (hunk_base + hunk_low_used);
	cs = cache_head.next;
	
	do
	{
		if (!nobottom || cs != cache_head.next)
		{
			if ( (byte *)cs - (byte *)newc >= size)
			{	// found space
				memset (newc, 0, sizeof(*newc));
				newc->size = size;
				
				newc->next = cs;
				newc->prev = cs->prev;
				cs->prev->next = newc;
				cs->prev = newc;
				
				Cache_MakeLRU (newc);
	
				return newc;
			}
		}

	// continue looking		
		newc = (cache_system_t *)((byte *)cs + cs->size);
		cs = cs->next;

	} while (cs != &cache_head);
	
// try to allocate one at the very end
	if ( hunk_base + hunk_size - hunk_high_used - (byte *)newc >= size)
	{
		memset (newc, 0, sizeof(*newc));
		newc->size = size;
		
		newc->next = &cache_head;
		newc->prev = cache_head.prev;
		cache_head.prev->next = newc;
		cache_head.prev = newc;
		
		Cache_MakeLRU (newc);

		return newc;
	}
	
	return NULL;		// couldn't allocate
}