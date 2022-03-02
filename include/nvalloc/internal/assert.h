#include "nvalloc/internal/util.h"
#include "stdio.h"
#include "stdlib.h"
#define config_debug 0
/*
 * Define a custom assert() in order to reduce the chances of deadlock during
 * assertion failure.
 */
#ifndef assert

#ifdef NVALLOC_DEBUG
#define assert(e)                                                \
	do                                                           \
	{                                                            \
		if (unlikely(!(e)))                                      \
		{                                                        \
			printf(                                              \
				"<nvalloc>: %s:%d: Failed assertion: \"%s\"\n", \
				__FILE__, __LINE__, #e);                         \
			abort();                                             \
		}                                                        \
	} while (0)
#else
#define assert(e)
#endif

#endif

#ifndef not_reached
#define not_reached()                                            \
	do                                                           \
	{                                                            \
		if (config_debug)                                        \
		{                                                        \
			printf(                                              \
				"<nvalloc>: %s:%d: Unreachable code reached\n", \
				__FILE__, __LINE__);                             \
			abort();                                             \
		}                                                        \
	} while (0)
#endif

#ifndef not_implemented
#define not_implemented()                                  \
	do                                                     \
	{                                                      \
		if (config_debug)                                  \
		{                                                  \
			printf("<nvalloc>: %s:%d: Not implemented\n", \
				   __FILE__, __LINE__);                    \
			abort();                                       \
		}                                                  \
	} while (0)
#endif

#ifndef assert_not_implemented
#define assert_not_implemented(e)           \
	do                                      \
	{                                       \
		if (unlikely(config_debug && !(e))) \
		{                                   \
			not_implemented();              \
		}                                   \
	} while (0)
#endif

/* Use to assert a particular configuration, e.g., cassert(config_debug). */
#ifndef cassert
#define cassert(c)          \
	do                      \
	{                       \
		if (unlikely(!(c))) \
		{                   \
			not_reached();  \
		}                   \
	} while (0)
#endif
