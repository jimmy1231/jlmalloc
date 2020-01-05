# jlmalloc
Dynamic memory allocator written in C. A modification of dlmalloc (by Doug Lea).
dlmalloc: http://gee.cs.oswego.edu/dl/html/malloc.html

#### Disclaimer
This allocator was implemented as part of the University of Toronto course ECE454 - Systems Programming.
The `brk()`and `sbrk()` system calls are mocked for internal testing purposes. As such, this allocator is
meant only for purposes of reference and not practice. <br/>
Please **do not** use this in any critical processes whatsoever as it was 
not rigorously tested against vast varieties of workloads.

---

### Init scheme:
Same as reference implementation
Make sure memory is 16-byte aligned at the beginning of payload section

---

### Malloc scheme:
- Index bin to find Free List to allocate from
    - Check FAST_BIN index by FAST_BIN[# of DWORDs]
    - If index within [0,16]:
        - Direct index into FAST_BIN to get Free List for the exact request size
        - If no chunks in direct index, keep searching bin until a non-empty Free List is found
    - Else:
        - Find Free List with a suitable size range in LOG_BIN
- Traverse Free List to find the chunk which is the best fit (sizeof chunk >= request size)
    - If chunk size = request size:
        - Remove chunk from head of Free List
        - Update chunk header & footer to allocated
        - Return pointer to 1st byte of payload - make sure it is 16-byte aligned
    - If chunk size > request size:
        - Remove chunk from Free List
        - Split chunk into 2 parts:
            - Sizeof Part_1 = request size
            - Sizeof Part_2 = (chunk size - request size)
        - Reinsert Part_2 into Free List:
            - Initialize this chunk with header & footer to allocated
            - Search for a bin which best fits its size
            - Insert it into the corresponding Free List in ascending size order
                - If sizes are the same, insert in ascending address order
            - If sizeof Part_2 < MIN_CHUNK_SIZE, do not insert it to Free List
        - Initialize Part_1 chunk with header & footer to allocated
        - Return pointer to 1st byte of payload of Part_1 - make sure it is 16-byte aligned
- If no chunks fit the request size in FAST_BIN and LOG_BIN:
    - If request size <= PREALLOC_CHUNK_SIZE:
        - Call sbrk(request_size * NUM_PREALLOC_CHUNKS) to allocate several new chunks of the exact request size
        - Initialize each chunk by adding header & footer and initialize to unallocated
        - Insert all but 1 allocated chunk into Free List:
            - Search for a bin which best fits the size
            - Insert it into the corresponding Free List in ascending size order
                - If sizes are the same, insert in ascending address order
    - Else if request size > PREALLOC_CHUNK_SIZE:
        - Call sbrk(request_size) to allocate 1 chunk of the exact request size
    - Update header & footer of the reserved chunk to allocated
    - Return pointer to 1st byte of payload - make sure it is 16-byte aligned

Important note: Once a chunk is allocated, its footer will be forfeited to the user.

---

### Free scheme:
- Reinstate chunk footer by copying value from header
- Update chunk status in header & footer to unallocated
- Coalesce with neighboring chunks:
    - If neighbor chunks are free - can coalesce:
        - Remove free neighbor(s) from respective Free List(s) (note: neighbor 1 and neighbor 2 could belong in different Free Lists)
        - Coalesce current free chunk with neighbor(s) by updating the size in the header & footer of boundary chunks
- Reinsert chunk into Free List:
    - Search for a bin which best fits its size
        - Insert into Free List in ascending size order
        - If sizes are the same, insert in ascending address order

---

### Realloc scheme:
- New Size = ALIGN_16(request size + overhead)
- If ptr == NUL:
    - Call malloc(New Size)
- If New Size == Current Size:
    - Return pointer to current chunk (trivial case)
- If New Size < Current Size:
    - Split current chunk into 2 parts (Part_1, Part_2):
        - Sizeof Part_1 = New Size
        - Sizeof Part_2 = (Current Size - New Size)
    - Free Part_2:
        - Initialize this chunk with header & footer to allocated
        - Call free() on this chunk
    - Initialize Part_1 chunk with header & footer to allocated
    - Return pointer to 1st byte of payload of Part_1 - make sure it is 16-byte aligned
- If New Size > Current Size:
    - malloc(request size)
    - memcpy data from current chunk into new chunk
    - free(current chunk)

---

### Optimizations:

##### Preallocation:
This helped in throughput tremendously - reasoning is that it is much faster to coalesce and then alloc than to call sbrk in the event that the heap needs to be extended. E.g. if we preallocate 20 chks of 144 bytes, those bytes can be used in 2 ways: 1) if further 144 requests come in, do not need to extend heap. 2) in the event of a free, by heuristics, we coalesce the neighboring blocks to form a bigger block, now imagine multiple frees in one memory region: multiple 144 bytes would form a giant block which can then be allocd on a large request where otherwise would a cause for extending heap

to preallocate: 10 for optimal performance in the general case, >10 for programs where a fixed amount of small chks (>304bytes) are used (allocd and freed), if this fixed amount of chks can be determined, one preallocation can account for all future allocations → huge performance & utilization gain

##### Deferred splice of large chks:
On a decrease realloc request, do not splice the chk to the lower size except in the case when the reallocd size is significantly less than the original chk size. This is to preempt a future increase realloc while keeping internal fragmentation at a minimum. Imagine the case where we do splice and free the upper half of the original chk, that chk is now subject to allocation from another request; then, if a future increase realloc were to happen, we have no choice but to extend heap and, if the chk size is large, leave massive external fragmentation.

##### Realloc on-demand coalescing:
On an increase realloc request, if neighboring chks are not allocd and satisfy the new size, coalesce with neighbors. This optimization potentially saves very expensive calls to malloc and free within realloc, its advantages are 2-fold: 1) Time - malloc and free are very expensive especially if extend heap is called - which involves sbrk; 2) Space - on if the current chk is very large, then there will most likely not be a free chk which the current chk can migrate to - so extend heap must be called which dramatically increases external fragmentation - this optimization capitalizes on opportunities to avoid heap extension with minimal negative side-effects. This is an important optimization and quite frequently occurring in realloc2-bal.rep. It turns out that often times, there will be free chks next to a large allocated chk perhaps as a side-effect of immediate coalescing on free.

##### Acceptable internal fragmentation:
Internal fragmentation for a large chk that is less than 5% of its size is an acceptable tradeoff. By accepting this fragmentation, malloc request with large sizes that are not exact fit to current free chks can be allocd from the current free chks. By doing this, we avoid potential calls to extend heap of no chks of exact fit is found.

##### Extend heap on realloc:
This is a targeted optimization for increase realloc requests of large allocd chks which are at the end of heap. The scheme is as follows: if a chk of large size is at end of heap, then alloc additional memory for the chk by extend the heap by a fraction of the current chk size such that it will satisfy the increased realloc size. The tradeoff here is that we assume this large size chk will grow in size with future increase realloc sizes, so we prealloc way more than the increase realloc size. This is an advanced optimization for rare, but costly case, where no free chks exist to satisfy the increased realloc size, so extend heap must be called which effectively doubles the heap size (it turns out that usually 100% of total program memory usage is from that one chk), and leaves significant external fragmentation.

This optimization works well only if the largest chks are pushed to the end of heap. Some strategies to do this in a segregated free list scheme:
- In programs with a fixed amount of small allocations (which display a pattern of repeated alloc and free) and only 1 large allocation which is repeatedly reallocd (nondecreasing in size), prealloc small allocations such that they will coalesce and push the large chk to end of heap
- Paging - phkmalloc - however this might do more harm to fragmentation than good

