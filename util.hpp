#ifndef UTIL_HPP
#define UTIL_HPP

template <typename MemType>
MemType hunkAlloc (int size);

template <typename MemType>
MemType hunkAllocName(int size, char *name);

template <typename MemType>
MemType zmalloc(int size);

template <typename MemType>
MemType SZGetSpace (sizebuf_t *buf, int length);

template <typename MemType>
MemType hunkTempAlloc(int size);
template <typename MemType>
MemType hunkHighAllocName (int size, char *name);

template <typename MemType>
MemType cacheAlloc (cache_user_t *c, int size, char *name);

template <typename MemType>
MemType cacheCheck (cache_user_t *c);
#endif