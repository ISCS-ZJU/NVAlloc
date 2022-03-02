/******************************************************************************/
#ifdef NVALLOC_H_TYPES

#define MAX_SZ ((1 << 13) + (1 << 11) * 3)

#define MAX_SZ_IDX 40

#define MAX_PSZ 196608

#define MAX_PSZ_IDX 63

#define SLAB_SCIND 65


typedef struct sizeclass_s sizeclass_t;
typedef struct psizeclass_s psizeclass_t;


#define SIZE_CLASSES                                                      \
	/* index, lg_grp, lg_delta, ndelta, psz, bin, pgs, lg_delta_lookup */ \
	SC(0, 3, 3, 0, no, yes, 1, 3)                                         \
	SC(1, 3, 3, 1, no, yes, 1, 3)                                         \
	SC(2, 3, 3, 2, no, yes, 3, 3)                                         \
	SC(3, 3, 3, 3, no, yes, 1, 3)                                         \
                                                                          \
	SC(4, 5, 3, 1, no, yes, 5, 3)                                         \
	SC(5, 5, 3, 2, no, yes, 3, 3)                                         \
	SC(6, 5, 3, 3, no, yes, 7, 3)                                         \
	SC(7, 5, 3, 4, no, yes, 1, 3)                                         \
                                                                          \
	SC(8, 6, 4, 1, no, yes, 5, 4)                                         \
	SC(9, 6, 4, 2, no, yes, 3, 4)                                         \
	SC(10, 6, 4, 3, no, yes, 7, 4)                                        \
	SC(11, 6, 4, 4, no, yes, 1, 4)                                        \
                                                                          \
	SC(12, 7, 5, 1, no, yes, 5, 5)                                        \
	SC(13, 7, 5, 2, no, yes, 3, 5)                                        \
	SC(14, 7, 5, 3, no, yes, 7, 5)                                        \
	SC(15, 7, 5, 4, no, yes, 1, 5)                                        \
                                                                          \
	SC(16, 8, 6, 1, no, yes, 5, 6)                                        \
	SC(17, 8, 6, 2, no, yes, 3, 6)                                        \
	SC(18, 8, 6, 3, no, yes, 7, 6)                                        \
	SC(19, 8, 6, 4, no, yes, 1, 6)                                        \
                                                                          \
	SC(20, 9, 7, 1, no, yes, 5, 7)                                        \
	SC(21, 9, 7, 2, no, yes, 3, 7)                                        \
	SC(22, 9, 7, 3, no, yes, 7, 7)                                        \
	SC(23, 9, 7, 4, no, yes, 1, 7)                                        \
                                                                          \
	SC(24, 10, 8, 1, no, yes, 5, 8)                                       \
	SC(25, 10, 8, 2, no, yes, 3, 8)                                       \
	SC(26, 10, 8, 3, no, yes, 7, 8)                                       \
	SC(27, 10, 8, 4, no, yes, 1, 8)                                       \
                                                                          \
	SC(28, 11, 9, 1, no, yes, 5, 9)                                       \
	SC(29, 11, 9, 2, no, yes, 3, 9)                                       \
	SC(30, 11, 9, 3, no, yes, 7, 9)                                       \
	SC(31, 11, 9, 4, yes, yes, 1, 9)                                      \
                                                                          \
	SC(32, 12, 10, 1, no, yes, 5, no)                                     \
	SC(33, 12, 10, 2, no, yes, 3, no)                                     \
	SC(34, 12, 10, 3, no, yes, 7, no)                                     \
	SC(35, 12, 10, 4, yes, yes, 2, no)                                    \
                                                                          \
	SC(36, 13, 11, 1, no, yes, 5, no)                                     \
	SC(37, 13, 11, 2, yes, yes, 3, no)                                    \
	SC(38, 13, 11, 3, no, yes, 7, no)

#define PSIZE_CLASSES                 \
	SC(31, 11, 9, 4, yes, no, 0, no)  \
	SC(35, 12, 10, 4, yes, no, 0, no) \
	SC(37, 13, 11, 2, yes, no, 0, no) \
	SC(39, 13, 11, 4, yes, no, 0, no) \
                                      \
	SC(40, 14, 12, 1, yes, no, 0, no) \
	SC(41, 14, 12, 2, yes, no, 0, no) \
	SC(42, 14, 12, 3, yes, no, 0, no) \
	SC(43, 14, 12, 4, yes, no, 0, no) \
                                      \
	SC(44, 15, 13, 1, yes, no, 0, no) \
	SC(45, 15, 13, 2, yes, no, 0, no) \
	SC(46, 15, 13, 3, yes, no, 0, no) \
	SC(47, 15, 13, 4, yes, no, 0, no) \
                                      \
	SC(48, 16, 14, 1, yes, no, 0, no) \
	SC(49, 16, 14, 2, yes, no, 0, no) \
	SC(50, 16, 14, 3, yes, no, 0, no) \
	SC(51, 16, 14, 4, yes, no, 0, no) \
                                      \
	SC(52, 17, 15, 1, yes, no, 0, no) \
	SC(53, 17, 15, 2, yes, no, 0, no) \
	SC(54, 17, 15, 3, yes, no, 0, no) \
	SC(55, 17, 15, 4, yes, no, 0, no) \
                                      \
	SC(56, 18, 16, 1, yes, no, 0, no) \
	SC(57, 18, 16, 2, yes, no, 0, no) \
	SC(58, 18, 16, 3, yes, no, 0, no) \
	SC(59, 18, 16, 4, yes, no, 0, no) \
                                      \
	SC(60, 19, 17, 1, yes, no, 0, no) \
	SC(61, 19, 17, 2, yes, no, 0, no) \
	SC(62, 19, 17, 3, yes, no, 0, no) \
	SC(63, 19, 17, 4, yes, no, 0, no) \
                                      \
	SC(64, 20, 18, 1, yes, no, 0, no) \
	SC(65, 20, 18, 2, yes, no, 0, no) \
	SC(66, 20, 18, 3, yes, no, 0, no) \
	SC(67, 20, 18, 4, yes, no, 0, no) \
                                      \
	SC(68, 21, 19, 1, yes, no, 0, no) \
	SC(69, 21, 19, 2, yes, no, 0, no) \
	SC(70, 21, 19, 3, yes, no, 0, no) \
	SC(71, 21, 19, 4, yes, no, 0, no) \
                                      \
	SC(72, 22, 20, 1, yes, no, 0, no) \
	SC(73, 22, 20, 2, yes, no, 0, no) \
	SC(74, 22, 20, 3, yes, no, 0, no) \
	SC(75, 22, 20, 4, yes, no, 0, no) \
                                      \
	SC(76, 23, 21, 1, yes, no, 0, no) \
	SC(77, 23, 21, 2, yes, no, 0, no) \
	SC(78, 23, 21, 3, yes, no, 0, no) \
	SC(79, 23, 21, 4, yes, no, 0, no) \
                                      \
	SC(80, 24, 22, 1, yes, no, 0, no) \
	SC(81, 24, 22, 2, yes, no, 0, no) \
	SC(82, 24, 22, 3, yes, no, 0, no) \
	SC(83, 24, 22, 4, yes, no, 0, no) \
                                      \
	SC(84, 25, 23, 1, yes, no, 0, no) \
	SC(85, 25, 23, 2, yes, no, 0, no) \
	SC(86, 25, 23, 3, yes, no, 0, no) \
	SC(87, 25, 23, 4, yes, no, 0, no) \
                                      \
	SC(88, 26, 24, 1, yes, no, 0, no) \
	SC(89, 26, 24, 2, yes, no, 0, no) \
	SC(90, 26, 24, 3, yes, no, 0, no) \
	SC(91, 26, 24, 4, yes, no, 0, no) \
                                      \
	SC(92, 27, 25, 1, yes, no, 0, no) \
	SC(93, 27, 25, 2, yes, no, 0, no) \
	SC(94, 27, 25, 3, yes, no, 0, no) \
	SC(95, 27, 25, 4, yes, no, 0, no) \
                                      \
	SC(96, 28, 26, 1, yes, no, 0, no) \
	SC(97, 28, 26, 2, yes, no, 0, no)



#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

struct sizeclass_s
{
	uint32_t moffset; 
	uint32_t roffset; 
	uint32_t nbits;
	uint8_t n_bitmaps;	
	uint16_t bitsize; 
#ifdef SLAB_MORPHING
	uint32_t toffset; 
#endif
};

struct psizeclass_s
{
	size_t size;
	size_t npages;
};

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

extern size_t psizeclass_lookup[MAX_PSZ + 1];
extern psizeclass_t psizeclasses[MAX_PSZ_IDX];


extern size_t sizeclass_lookup[MAX_SZ + 1];

extern sizeclass_t sizeclasses[MAX_SZ_IDX];

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

static inline sizeclass_t get_sizeclass_by_idx(size_t idx)
{
	return sizeclasses[idx];
}


static inline size_t get_sizeclass(size_t size)
{
	return sizeclass_lookup[size];
}

static inline size_t get_sizeclass_id_by_size(size_t size)
{
	return sizeclass_lookup[size];
}


static inline size_t get_sizeclass_id_by_sc(sizeclass_t *sc)
{
	return get_sizeclass_id_by_size(sc->bitsize);
}



static inline psizeclass_t *get_psizeclass_by_idx(size_t idx)
{
	return &psizeclasses[idx];
}

static inline size_t get_psizeclass(size_t npages)
{
	assert(npages <= MAX_PSZ);
	assert(psizeclass_lookup[npages] < MAX_PSZ_IDX);
	return psizeclass_lookup[npages];
}

int sizeclass_boot();


#ifdef SLAB_MORPHING
void sizeclass_reset(sizeclass_t * sc, size_t table_size);
#endif

#endif /* NVALLOC_H_INLINES */
	   /******************************************************************************/