#define CACHE_LINE_SIZE 64


static inline void clwb(void *data, int len)
{
    char *ptr = (char *)((uint64_t)data & ~(CACHE_LINE_SIZE - 1));
    for (; (intptr_t)ptr < (intptr_t)data + len; ptr = (char *)((intptr_t)ptr + CACHE_LINE_SIZE))
    {
        {

            _mm_clwb((void *)ptr);
        }
    }
}

static inline void persist(void *ptr, int size)
{
    clwb(ptr, size);
    _mm_sfence();
}