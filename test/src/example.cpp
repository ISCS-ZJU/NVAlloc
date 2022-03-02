#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libpmem.h>
#include <unistd.h>
#include "memory.h"
#include "sys/mman.h"
#include "x86intrin.h"
#include <sys/time.h>
#include <pthread.h>

#include "nvalloc.h"

#define NTIMES 1000000

int times = NTIMES;

typedef struct foo_s
{
    int64_t a[8];
} foo_t;

int main(int argc, char *argv[])
{

    nvalloc_init();

    struct timeval start;
    struct timeval end;
    unsigned long diff;
    gettimeofday(&start, NULL);

    foo_t **a = (foo_t **)malloc(8 * times);

    for (int i = 0; i < times; i++)
    {

        nvalloc_malloc_to(64, (void **)&a[i]);


    }

    for (int i = 0; i < times; i++)
    {
        nvalloc_free_from((void **)&a[i]);
    }

    _mm_mfence();
    gettimeofday(&end, NULL);

    diff = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;

    printf("Total time is %ld us\n", diff);
}