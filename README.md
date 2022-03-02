# NVAlloc: Rethinking Heap Metadata Management in Persistent Memory Allocators

NVAlloc is a heap memory allocator for persistent memory (e.g., Intel Optane DCPMM); it emphasizes efficiently eliminating cache line reflushes and small random writes and alleviating slab-induced memory fragmentation in heap metadata management. NVAlloc has three new techniques:
* Interleaved Mapping
* Log-structured Bookkeeping
* Slab Morphing

Please read the following paper for more details: 

[Zheng Dang, Shuibing He, Peiyi Hong, Zhenxin Li, Xuechen Zhang, Xian-He Sun, and Gang Chen. NVAlloc: rethinking heap metadata management in persistent memory allocators. ASPLOS 2022.](https://dl.acm.org/doi/10.1145/3503222.3507743)

## Warning

The code can only be used for academic research.

We use part of jemalloc's code in our source code, please check jemalloc's [COPYRIGHT](https://github.com/jemalloc/jemalloc/blob/master/COPYING).


## Directories

* bin : output executable files
* lib : output library files
* include : header files
* include/internal/nvalloc_internal.h : It includes all header files, and should be included by all source files.
* src : source files
* test/src: test source files


## System Requirements

1. 2nd Generation Intel Xeon Scalable Processors
2. Intel® Optane™ Persistent Memory 100 Series
3. cmake (version > 3.10)
4. libpmem
5. libjemalloc


## Build

```
autogen.sh [d/r]
autogen.sh [d/r] [testname]
```
autogen.sh encapsulates CMakeLists.txt:
* [**d/r**] : debug/release
* [**testname**] : build test/src/**testname**.cpp

## Other

Default pmem directory: /mnt/pmem/nvalloc_files/

## Example

```
./autogen.sh r example    # test/src/example.cpp
./bin/example
```