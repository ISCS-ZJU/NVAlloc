
#define NVALLOC_XX_C_
#include "nvalloc/internal/nvalloc_internal.h"

/******************************************************************************/


#ifdef SLAB_MORPHING
#define SIZE_CLASS_bin_yes(bitsize) \
	{0, 0, 0, 0, bitsize, 0},
#else
#define SIZE_CLASS_bin_yes(bitsize) \
	{0, 0, 0, 0, bitsize},
#endif

#define SIZE_CLASS_bin_no(bitsize) \
	{bitsize, bitsize / PAGE_SIZE},

#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup) \
	SIZE_CLASS_bin_##bin(((1U << lg_grp) + (ndelta << lg_delta)))


size_t sizeclass_lookup[MAX_SZ + 1];

sizeclass_t sizeclasses[MAX_SZ_IDX] = {{0, 0, 0, 0}, SIZE_CLASSES};

size_t psizeclass_lookup[MAX_PSZ + 1];
psizeclass_t psizeclasses[MAX_PSZ_IDX] = {PSIZE_CLASSES};

/******************************************************************************/
/* Function prototypes for non-inline static functions. */

/******************************************************************************/
/* Inline tool function */

/******************************************************************************/

static inline int int_align_to(int x, int align)
{
	int i = x / align;
	if (x % align != 0)
		++i;
	return i * align;
}

static inline uint32_t get_meta_size(int nbits)
{
	return roundUp( (uint32_t)ceil(nbits/8.0), (CACHELINE_SIZE * NBANKS));
}


int sizeclass_boot()
{
	size_t lookupIdx = 0;
	size_t plookupIdx = 0;
	for (size_t sc_idx = 1; sc_idx < MAX_SZ_IDX; sc_idx++)
	{
		size_t bitsize = sizeclasses[sc_idx].bitsize;

		while (lookupIdx <= bitsize)
		{
			sizeclass_lookup[lookupIdx] = sc_idx;
			++lookupIdx;
		}
	}

	for (size_t psc_idx = 0; psc_idx < MAX_PSZ_IDX; psc_idx++)
	{
		size_t npages = psizeclasses[psc_idx].npages;
		while (plookupIdx <= npages)
		{
			psizeclass_lookup[plookupIdx] = psc_idx;
			++plookupIdx;
		}
	}


	for (int i = 1; i < MAX_SZ_IDX; i++)
	{
		sizeclass_t *sizeclass = &sizeclasses[i];
		size_t bitsize = sizeclass->bitsize;
		int nbits = SLAB_SIZE / bitsize + 1;
		size_t total_size;
		int head_size = roundUp(sizeof(slab_t), CACHELINE_SIZE);

		for (;; nbits--)
		{
			total_size = nbits * bitsize + head_size + get_meta_size(nbits);
			if (total_size <= SLAB_SIZE)
				break;
		}
		sizeclass->nbits = nbits;
		sizeclass->moffset = roundUp(sizeof(slab_t), CACHELINE_SIZE);
#ifdef SLAB_MORPHING
		sizeclass->toffset = 0;
#endif
		sizeclass->roffset = sizeclass->moffset + get_meta_size(nbits);
		sizeclass->n_bitmaps = (int)(ceil(nbits / 64.0)); 

		uint32_t tmp = get_meta_size(nbits+1);
		if (tmp > meta_size)
		{
			meta_size = tmp;
		}
	}
	return 0;
}


#ifdef SLAB_MORPHING
void sizeclass_reset(sizeclass_t *sc, size_t table_size)
{
	int nbits = SLAB_SIZE / sc->bitsize + 1;
	size_t bitsize = sc->bitsize;
	size_t total_size;
	int head_size = roundUp(sizeof(slab_t), CACHELINE_SIZE);

	for (;; nbits--)
	{
		total_size = nbits * bitsize + head_size + meta_size + table_size;
		if (total_size <= SLAB_SIZE)
			break;
	}
	sc->nbits = nbits;
	sc->toffset = sc->moffset + meta_size;
	sc->roffset = sc->toffset + table_size;
	sc->n_bitmaps = (int)(ceil(nbits / 64.0)); 
}
#endif