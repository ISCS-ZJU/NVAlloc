#define NVALLOC_INTERNAL_TYPES_H

/* Return the smallest cacheline multiple that is >= s. */
#define CACHELINE_CEILING(s)						\
	(((s) + CACHELINE_MASK) & ~CACHELINE_MASK)

/* Return the nearest aligned address at or below a. */
#define ALIGNMENT_ADDR2BASE(a, alignment)				\
	((void *)((uintptr_t)(a) & ((~(alignment)) + 1)))

/* Return the offset between a and the nearest aligned address at or below a. */
#define ALIGNMENT_ADDR2OFFSET(a, alignment)				\
	((size_t)((uintptr_t)(a) & (alignment - 1)))

/* Return the smallest alignment multiple that is >= s. */
#define ALIGNMENT_CEILING(s, alignment)					\
	(((s) + (alignment - 1)) & ((~(alignment)) + 1))
	
static inline uint64_t roundUp(uint64_t numToRound, uint64_t multiple)
{
    if (multiple == 0)
        return numToRound;

    int remainder = numToRound % multiple;
    if (remainder == 0)
        return numToRound;

    return numToRound + multiple - remainder;
}