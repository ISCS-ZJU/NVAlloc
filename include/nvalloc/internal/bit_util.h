#ifndef JEMALLOC_INTERNAL_BIT_UTIL_H
#define JEMALLOC_INTERNAL_BIT_UTIL_H

#include "nvalloc/internal/assert.h"

#define ZU(z) ((size_t)z)
#define ZD(z) ((ssize_t)z)
#define QU(q) ((uint64_t)q)
#define QD(q) ((int64_t)q)

#define KZU(z) ZU(z##ULL)
#define KZD(z) ZD(z##LL)
#define KQU(q) QU(q##ULL)

/*
 * ffs*() functions to use for bitmapping.  Don't use these directly; instead,
 * use ffs_*() from util.h.
 */
#define JEMALLOC_INTERNAL_FFSLL __builtin_ffsll
#define JEMALLOC_INTERNAL_FFSL __builtin_ffsl
#define JEMALLOC_INTERNAL_FFS __builtin_ffs
/*
 * popcount*() functions to use for bitmapping.
 */
#define JEMALLOC_INTERNAL_POPCOUNTL __builtin_popcountl
#define JEMALLOC_INTERNAL_POPCOUNT __builtin_popcount

#define BIT_UTIL_INLINE static inline

/* Sanity check. */
#if !defined(JEMALLOC_INTERNAL_FFSLL) || !defined(JEMALLOC_INTERNAL_FFSL) || !defined(JEMALLOC_INTERNAL_FFS)
#error JEMALLOC_INTERNAL_FFS{,L,LL} should have been defined by configure
#endif

BIT_UTIL_INLINE unsigned
ffs_llu(unsigned long long bitmap)
{
	return JEMALLOC_INTERNAL_FFSLL(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_lu(unsigned long bitmap)
{
	return JEMALLOC_INTERNAL_FFSL(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_u(unsigned bitmap)
{
	return JEMALLOC_INTERNAL_FFS(bitmap);
}

#ifdef JEMALLOC_INTERNAL_POPCOUNTL
BIT_UTIL_INLINE unsigned
popcount_lu(unsigned long bitmap)
{
	return JEMALLOC_INTERNAL_POPCOUNTL(bitmap);
}
#endif

/*
 * Clears first unset bit in bitmap, and returns
 * place of bit.  bitmap *must not* be 0.
 */

BIT_UTIL_INLINE size_t
cfs_lu(unsigned long *bitmap)
{
	size_t bit = ffs_lu(*bitmap) - 1;
	*bitmap ^= ZU(1) << bit;
	return bit;
}

BIT_UTIL_INLINE unsigned
ffs_zu(size_t bitmap)
{

	return ffs_lu(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_u64(uint64_t bitmap)
{

	return ffs_lu(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_u32(uint32_t bitmap)
{

	return ffs_u(bitmap);
}

BIT_UTIL_INLINE uint64_t
pow2_ceil_u64(uint64_t x)
{

	if (unlikely(x <= 1))
	{
		return x;
	}
	size_t msb_on_index;

	asm("bsrq %1, %0"
		: "=r"(msb_on_index) // Outputs.
		: "r"(x - 1)		 // Inputs.
	);

	assert(msb_on_index < 63);
	return 1ULL << (msb_on_index + 1);
}

BIT_UTIL_INLINE uint32_t
pow2_ceil_u32(uint32_t x)
{

	if (unlikely(x <= 1))
	{
		return x;
	}
	size_t msb_on_index;

	msb_on_index = (31 ^ __builtin_clz(x - 1));

	assert(msb_on_index < 31);
	return 1U << (msb_on_index + 1);
}

/* Compute the smallest power of 2 that is >= x. */
BIT_UTIL_INLINE size_t
pow2_ceil_zu(size_t x)
{

	return pow2_ceil_u64(x);
}

BIT_UTIL_INLINE unsigned
lg_floor(size_t x)
{
	size_t ret;
	assert(x != 0);

	asm("bsr %1, %0"
		: "=r"(ret) // Outputs.
		: "r"(x)	// Inputs.
	);
	assert(ret < UINT_MAX);
	return (unsigned)ret;
}

BIT_UTIL_INLINE unsigned
lg_ceil(size_t x)
{
	return lg_floor(x) + ((x & (x - 1)) == 0 ? 0 : 1);
}

#undef BIT_UTIL_INLINE

/* A compile-time version of lg_floor and lg_ceil. */
#define LG_FLOOR_1(x) 0
#define LG_FLOOR_2(x) (x < (1ULL << 1) ? LG_FLOOR_1(x) : 1 + LG_FLOOR_1(x >> 1))
#define LG_FLOOR_4(x) (x < (1ULL << 2) ? LG_FLOOR_2(x) : 2 + LG_FLOOR_2(x >> 2))
#define LG_FLOOR_8(x) (x < (1ULL << 4) ? LG_FLOOR_4(x) : 4 + LG_FLOOR_4(x >> 4))
#define LG_FLOOR_16(x) (x < (1ULL << 8) ? LG_FLOOR_8(x) : 8 + LG_FLOOR_8(x >> 8))
#define LG_FLOOR_32(x) (x < (1ULL << 16) ? LG_FLOOR_16(x) : 16 + LG_FLOOR_16(x >> 16))
#define LG_FLOOR_64(x) (x < (1ULL << 32) ? LG_FLOOR_32(x) : 32 + LG_FLOOR_32(x >> 32))

#define LG_FLOOR(x) LG_FLOOR_64((x))

#define LG_CEIL(x) (LG_FLOOR(x) + (((x) & ((x)-1)) == 0 ? 0 : 1))

#endif /* JEMALLOC_INTERNAL_BIT_UTIL_H */
