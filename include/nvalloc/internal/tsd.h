
#include "nvalloc/internal/rtree_tsd.h"
#ifdef NVALLOC_H_TYPES

/*
 * TLS/TSD-agnostic macro-based implementation of thread-specific data.  There
 * are four macros that support (at least) three use cases: file-private,
 * library-private, and library-private inlined.  Following is an example
 * library-private tsd variable:
 *
 * In example.h:
 *   typedef struct {
 *           int x;
 *           int y;
 *   } example_t;
 *   #define EX_INITIALIZER NVALLOC_CONCAT({0, 0})
 *   malloc_tsd_protos(, example, example_t *)
 *   malloc_tsd_externs(example, example_t *)
 * In example.c:
 *   malloc_tsd_data(, example, example_t *, EX_INITIALIZER)
 *   malloc_tsd_funcs(, example, example_t *, EX_INITIALIZER,
 *       example_tsd_cleanup)
 *
 * The result is a set of generated functions, e.g.:
 *
 *   bool example_tsd_boot(void) {...}
 *   example_t **example_tsd_get() {...}
 *   void example_tsd_set(example_t **val) {...}
 *
 * Note that all of the functions deal in terms of (a_type *) rather than
 * (a_type)  so that it is possible to support non-pointer types (unlike
 * pthreads TSD).  example_tsd_cleanup() is passed an (a_type *) pointer that is
 * cast to (void *).  This means that the cleanup function needs to cast *and*
 * dereference the function argument, e.g.:
 *
 *   void
 *   example_tsd_cleanup(void *arg)
 *   {
 *           example_t *example = *(example_t **)arg;
 *
 *           [...]
 *           if ([want the cleanup function to be called again]) {
 *                   example_tsd_set(&example);
 *           }
 *   }
 *
 * If example_tsd_set() is called within example_tsd_cleanup(), it will be
 * called again.  This is similar to how pthreads TSD destruction works, except
 * that pthreads only calls the cleanup function again if the value was set to
 * non-NULL.
 */

/* malloc_tsd_protos(). */
#define malloc_tsd_protos(a_attr, a_name, a_type) \
	a_attr bool                                   \
		a_name##_tsd_boot(void);                  \
	a_attr a_type *                               \
		a_name##_tsd_get(void);                   \
	a_attr void                                   \
		a_name##_tsd_set(a_type *val);

/* malloc_tsd_externs(). */
#define malloc_tsd_externs(a_name, a_type) \
	extern __thread a_type a_name##_tls;   \
	extern pthread_key_t a_name##_tsd;     \
	extern bool a_name##_booted;

/* malloc_tsd_data(). */
#define malloc_tsd_data(a_attr, a_name, a_type, a_initializer) \
	a_attr __thread a_type                                     \
		a_name##_tls = a_initializer;                          \
	a_attr pthread_key_t a_name##_tsd;                         \
	a_attr bool a_name##_booted = false;

/* malloc_tsd_funcs(). */
#define malloc_tsd_funcs(a_attr, a_name, a_type, a_initializer,    \
						 a_cleanup)                                \
	/* Initialization/cleanup. */                                  \
	a_attr bool                                                    \
		a_name##_tsd_boot(void)                                    \
	{                                                              \
                                                                   \
		if (a_cleanup != malloc_tsd_no_cleanup)                    \
		{                                                          \
			if (pthread_key_create(&a_name##_tsd, a_cleanup) != 0) \
				return (true);                                     \
		}                                                          \
		a_name##_booted = true;                                    \
		return (false);                                            \
	}                                                              \
	/* Get/set. */                                                 \
	a_attr a_type *                                                \
		a_name##_tsd_get(void)                                     \
	{                                                              \
		return (&a_name##_tls);                                    \
	}                                                              \
	a_attr void                                                    \
		a_name##_tsd_set(a_type *val)                              \
	{                                                              \
		a_name##_tls = (*val);                                     \
	}

#endif /* NVALLOC_H_TYPES */
/******************************************************************************/
#ifdef NVALLOC_H_STRUCTS

#endif /* NVALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef NVALLOC_H_EXTERNS

void malloc_tsd_no_cleanup(void *);

#endif /* NVALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef NVALLOC_H_INLINES

#endif /* NVALLOC_H_INLINES */
/******************************************************************************/
