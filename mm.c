/*
 * This implementation replicates the implicit list implementation
 * provided in the textbook
 * "Computer Systems - A Programmer's Perspective"
 * Blocks are never coalesced or reused.
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#define MM_IMPL

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ME BIG BRAIN",
    /* First member's full name */
    "Jing Yi Li",
    /* First member's email address */
    "dfjimmy.li@mail.utoronto.ca",
    /* Second member's full name (do not modify this as this is an individual lab) */
    "",
    /* Second member's email address (do not modify this as this is an individual lab)*/
    ""
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes), x86-64 -> 8 bytes, x86 -> 4 bytes */
#define DSIZE       (2 * WSIZE)               /* doubleword size (bytes) 8*2=16 bytes */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXTP(bp)       ((char *)(bp) + WSIZE)
#define PREVP(bp)       (bp)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define NEXT_FL_BLKP(bp) ((void *)GET(NEXTP(bp)))
#define PREV_FL_BLKP(bp) ((void *)GET(PREVP(bp)))

#define IS_WORD_ALIGNED(addr) ((((uintptr_t) addr) % WSIZE) == 0)
#define IS_DWORD_ALIGNED(addr) ((((uintptr_t) addr) % DSIZE) == 0)

#define IS_ASSERT 1
#define IS_DEBUG 1
#define OPT_REALLOC 1
#define OPT_COALESCE 0

#define PRINT_CHK(str, bp) \
  printf(str); \
  printf("<%d|%d, 0x%x>\n", (int)GET_SIZE(HDRP(bp)), \
    (int)GET_ALLOC(HDRP(bp)), (unsigned int)bp);

#define POW_2(pow) (1<<(pow))

#define MIN_CHK_SIZE DSIZE * 2
#define MAX_BIN_SIZE 32
#define NUM_FAST_BIN 17
#define LEAST_LOG_BIN_POW 9
#define NUM_CHK_PREALLOC 10
#define NUM_CHK_PREALLOC_ADV 30
#define MIN_LOG_CHK_SIZE (DSIZE * (NUM_FAST_BIN+2))

#if IS_DEBUG
size_t P_k = 0;
size_t Mem_usr_allocd = 0;
int num_16_byte_requests = 0;
#endif
#if IS_ASSERT && IS_DEBUG
#define STAT_REPORT_INTERVAL 200
int num_requests = 0;
#endif

/* This is the segregated free list
 * FAST_BIN: [0-16]
 * LOG_BIN: [17-32]
 */
void* FAST_LOG_BIN[MAX_BIN_SIZE];

static size_t __find_fit_ind(size_t asize);
static void *__flinsert(void *bp, void **FREE_LIST_p);
static int __flremove(void *bp, void **FREE_LIST_p);
static void __printfl(void *FREE_LIST);
static void __printheap(void);
static void mm_check(void);

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 *
 * (1) Extend heap 4*WSIZE=4*8=32 bytes
 * (2) ld 0 -> 0(heap_listp)
 * (3) ld something -> 8(heap_listp)
 * (4) ld something -> 16(heap_listp)
 * (5) ld something -> 24(heap_listp)
 * (6) addi heap_listp, 16 -> heap_listp
 **********************************************************/
 int mm_init(void)
 {
   /*
    * Steps:
    * (1) Add 8 bytes of padding to make payload of 1st block
    *     DWORD aligned
    * (2) Initialize segregated list for both FAST and LOG to
    *     NULL
    */
   void *heap_listp;
   if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) -1)
     return -1;

   PUT(heap_listp, 0); // alignment padding & prologue
   PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));   // prologue header
   PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));   // prologue footer
   PUT(heap_listp + (3 * WSIZE), PACK(0, 1));       // epilogue

#if IS_ASSERT
   assert(IS_DWORD_ALIGNED(heap_listp));
#if IS_DEBUG
   uintptr_t *ptr = heap_listp;
   P_k = 0;
   Mem_usr_allocd = 0;
   num_16_byte_requests = 0;
   printf("0x%x + 1 = 0x%x\n", &*(ptr), &*(ptr+1));
   printf("TOP_OF_HEAP=0x%x, BOTTOM_OF_HEAP=0x%x\n",
     (int)mem_heap_hi(), (int)mem_heap_lo());
#endif
#endif

   int i;
   for (i = 0; i < MAX_BIN_SIZE; i++) {
     FAST_LOG_BIN[i] = NULL;
   }

   return 0;
 }

static void *__chk_coalesce(void *bp) {
  /*
   * 4 Cases:
   * (1) Both neighbour chunks are allocated
   * (2) Only next chunk is not allocated
   * (3) Only prev chunk is not allocated
   * (4) Both neighbour chunks are not allocated
   *
   * Optimization:
   * - Only coalesce when chunk size is below a threshold
   */
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));
  size_t bin_ind;
  void *prev_bp, *next_bp;

#if IS_ASSERT
  void *next_hdrp = HDRP(NEXT_BLKP(bp));
  void *prev_hdrp = HDRP(PREV_BLKP(bp));
  void *next_ftrp = FTRP(NEXT_BLKP(bp));
  void *prev_ftrp = FTRP(PREV_BLKP(bp));
#if IS_DEBUG
  printf("LAST_BYTE_OF_HEAP=0x%x, NEXT_HDRP=0x%x\n",
    (int)mem_heap_hi(), (int)NEXT_BLKP(bp));
#endif
  if (mem_heap_hi() == NEXT_BLKP(bp)-1) {
    // check for epilogue
    assert(GET_ALLOC(next_hdrp) == 1);
    assert(GET_SIZE(next_hdrp) == 0);
  } else {
#if IS_DEBUG
    PRINT_CHK("NEXT_CHK: ", NEXT_BLKP(bp));
#endif
    assert(GET_SIZE(next_hdrp) == GET_SIZE(next_ftrp));
    assert(GET_SIZE(prev_hdrp) == GET_SIZE(prev_ftrp));
    assert(GET_ALLOC(next_hdrp) == GET_ALLOC(next_ftrp));
    assert(GET_ALLOC(prev_hdrp) == GET_ALLOC(prev_ftrp));
  }
#if IS_DEBUG
  PRINT_CHK("CURR_CHK: ", bp);
  PRINT_CHK("PREV_CHK: ", PREV_BLKP(bp));
#endif
#endif

  /* Small blocks should not coalesce with same size! */
#if OPT_COALESCE
  if (size < MIN_LOG_CHK_SIZE) {
    if (GET_SIZE(FTRP(PREV_BLKP(bp))) == size &&
      GET_SIZE(HDRP(NEXT_BLKP(bp))) == size) {
      prev_alloc = 1;
      next_alloc = 1;
    }
  }
#endif

  // (1)
  if (prev_alloc && next_alloc) {
    return bp;
  }
  // (2)
  else if (prev_alloc && !next_alloc) {
    next_bp = NEXT_BLKP(bp);
    bin_ind = __find_fit_ind(GET_SIZE(HDRP(next_bp)));
    assert(__flremove(next_bp, &FAST_LOG_BIN[bin_ind]));

    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    return bp;
  }
  // (3)
  else if (!prev_alloc && next_alloc) {
    prev_bp = PREV_BLKP(bp);
    bin_ind = __find_fit_ind(GET_SIZE(HDRP(prev_bp)));
    assert(__flremove(prev_bp, &FAST_LOG_BIN[bin_ind]));

    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

    return PREV_BLKP(bp);
  }
  // (4)
  else {
    prev_bp = PREV_BLKP(bp);
    next_bp = NEXT_BLKP(bp);
    bin_ind = __find_fit_ind(GET_SIZE(HDRP(prev_bp)));
    assert(__flremove(prev_bp, &FAST_LOG_BIN[bin_ind]));
    bin_ind = __find_fit_ind(GET_SIZE(FTRP(next_bp)));
    assert(__flremove(next_bp, &FAST_LOG_BIN[bin_ind]));

    size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(FTRP(next_bp));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));

    return PREV_BLKP(bp);
  }
}

static int __flremove(void *bp, void **FREE_LIST_p) {
  /*
   * 3 Cases:
   * (1) Chunk is at beginning of FREE_LIST
   * (2) Chunk is in middle of FREE_LIST
   * (3) Chunk is at end of FREE_LIST
   */
#if IS_ASSERT
  /* Get # of chunks in free list, meanwhile, check if FL is sorted */
  void *asrt_bp = *FREE_LIST_p;
  int num_chunks = 0;
  size_t last_size = 0;
#if IS_DEBUG
  __printfl(*FREE_LIST_p);
#endif
  while (asrt_bp != NULL) {
    assert(GET_SIZE(HDRP(asrt_bp)) >= last_size);
    num_chunks++;
    asrt_bp = NEXT_FL_BLKP(asrt_bp);
  }
#endif

  void *next_bp, *prev_bp;
  // Case (1)
  if (PREV_FL_BLKP(bp) == 0) {
#if IS_DEBUG
    PRINT_CHK("REMOVE - Case 1: ", bp);
#endif
    next_bp = NEXT_FL_BLKP(bp);
    if (next_bp != NULL)
      PUT(PREVP(next_bp), 0);

    *FREE_LIST_p = next_bp;
  }
  // Case (2)
  else if ((prev_bp = PREV_FL_BLKP(bp)) != 0 &&
              (next_bp = NEXT_FL_BLKP(bp)) != 0) {
#if IS_DEBUG
    PRINT_CHK("REMOVE - Case 2: ", bp);
#endif
    PUT(NEXTP(prev_bp), (uintptr_t) next_bp);
    PUT(PREVP(next_bp), (uintptr_t) prev_bp);
  }
  // Case (3)
  else if (NEXT_FL_BLKP(bp) == 0) {
#if IS_DEBUG
    PRINT_CHK("REMOVE - Case 3: ", bp);
#endif
    prev_bp = PREV_FL_BLKP(bp);
    if (prev_bp != NULL)
      PUT(NEXTP(prev_bp), 0);
  }

#if IS_ASSERT
  /* Verify # of chunks in free list is one less */
  asrt_bp = *FREE_LIST_p;
  int num_chunks_rm = 0;
  last_size = 0;
#if IS_DEBUG
  __printfl(*FREE_LIST_p);
#endif
  while (asrt_bp != NULL) {
    assert(GET_SIZE(HDRP(asrt_bp)) >= last_size);
    num_chunks_rm++;
    asrt_bp = NEXT_FL_BLKP(asrt_bp);
  }
  assert(num_chunks_rm == num_chunks-1);
#endif

  return 1;
}

static void *__flinsert(void *bp, void **FREE_LIST_p) {
  /*
   * Steps:
   * (1) Sort by size, keep traversing list until find a chunk
   *     which has >= size to sizeof the inserting chunk
   *      - if greater than, insert before
   *      - if chunks are equal in size:
   *          - compare addresses: if current chunk has lower
   *            address, insert it before, else, insert after
   * Special cases:
   *  - If FREE_LIST is NULL, then insert at head
   */
#if IS_DEBUG
  PRINT_CHK("INSERT_CHK: ", bp);
#endif

#if IS_ASSERT
  /* Get # of chunks in free list, meanwhile, check if FL is sorted */
  void *asrt_bp = *FREE_LIST_p;
  int num_chunks = 0;
  size_t last_size = 0;
#if IS_DEBUG
  __printfl(*FREE_LIST_p);
#endif
  while (asrt_bp != NULL) {
    assert(GET_SIZE(HDRP(asrt_bp)) >= last_size);
    num_chunks++;
    asrt_bp = NEXT_FL_BLKP(asrt_bp);
  }
#endif

  /* Case 0: Insert into an empty list */
  if (*FREE_LIST_p == NULL) {
#if IS_DEBUG
    printf("\tCase 0\n");
#endif
    PUT(PREVP(bp), 0);
    PUT(NEXTP(bp), 0);
    *FREE_LIST_p = bp;
#if IS_DEBUG
    __printfl(*FREE_LIST_p);
#endif
    return bp;
  }

  void *bp_c = *FREE_LIST_p, *prev_bp, *saved_bp = NULL;
  int is_inserted = 0, i;
  size_t BP_SIZE = GET_SIZE(HDRP(bp));
  size_t BP_C_SIZE;
  for (i = 0; bp_c != NULL; bp_c = NEXT_FL_BLKP(bp_c), i++) {
    saved_bp = bp_c;
    BP_C_SIZE = GET_SIZE(HDRP(bp_c));
    /*
     * Case 1: Insert at beginning of list
     * Conditions:
     * (1) If sizeof bp_c > BP_SIZE and index == 0
     * (2) If addr_of bp_c > addr_of bp and index == 0
     *
     * Case 2: Insert in middle of list
     * Conditions:
     * (1) If sizeof bp_c > BP_SIZE and index != 0
     * (2) If addr_of bp_c > addr_of bp and index != 0
     */
    if ((BP_C_SIZE > BP_SIZE && i == 0)) {
      // Case 1 (1)
#if IS_ASSERT
#if IS_DEBUG
      printf("\tCase 1(1)\n");
#endif
      assert(*FREE_LIST_p == bp_c);
      assert(PREV_FL_BLKP(bp_c) == NULL);
#endif
      PUT(PREVP(bp_c), (uintptr_t) bp);
      PUT(NEXTP(bp), (uintptr_t) bp_c);
      PUT(PREVP(bp), 0);
      *FREE_LIST_p = bp;

      is_inserted = 1;
      break;
    } else if (BP_C_SIZE > BP_SIZE) {
      // Case 2 (1)
#if IS_ASSERT
#if IS_DEBUG
      printf("\tCase 2(1)\n");
#endif
      assert(PREV_FL_BLKP(bp_c) != NULL);
#endif
      prev_bp = PREV_FL_BLKP(bp_c);
      PUT(NEXTP(prev_bp), (uintptr_t) bp);
      PUT(PREVP(bp_c), (uintptr_t) bp);
      PUT(NEXTP(bp), (uintptr_t) bp_c);
      PUT(PREVP(bp), (uintptr_t) prev_bp);

      is_inserted = 1;
      break;
    }

    if (BP_C_SIZE == BP_SIZE) {
      if (bp_c > bp && i == 0) {
        // Case 1 (2)
#if IS_ASSERT
#if IS_DEBUG
        printf("\tCase 1(2)\n");
#endif
        assert(*FREE_LIST_p == bp_c);
        assert(PREV_FL_BLKP(bp_c) == NULL);
#endif
        PUT(PREVP(bp_c), (uintptr_t) bp);
        PUT(NEXTP(bp), (uintptr_t) bp_c);
        PUT(PREVP(bp), 0);
        *FREE_LIST_p = bp;

        is_inserted = 1;
        break;
      } else if (bp_c > bp) {
        // Case 2 (2)
#if IS_ASSERT
#if IS_DEBUG
        printf("\tCase 2(2)\n");
#endif
        assert(PREV_FL_BLKP(bp_c) != NULL);
#endif
        prev_bp = PREV_FL_BLKP(bp_c);
        PUT(NEXTP(prev_bp), (uintptr_t) bp);
        PUT(PREVP(bp_c), (uintptr_t) bp);
        PUT(NEXTP(bp), (uintptr_t) bp_c);
        PUT(PREVP(bp), (uintptr_t) prev_bp);

        is_inserted = 1;
        break;
      }
    }
  }

  if (!is_inserted) {
    /* Case 3: Insert at end of list */
#if IS_ASSERT
#if IS_DEBUG
    printf("\tCase 3\n");
#endif
    assert(saved_bp != NULL && NEXT_FL_BLKP(saved_bp) == NULL);
#endif
    PUT(NEXTP(saved_bp), (uintptr_t) bp);
    PUT(PREVP(bp), (uintptr_t) saved_bp);
    PUT(NEXTP(bp), 0);
  }

#if IS_ASSERT
  /*
   * Verify that # chunks in free list increased by 1
   * Also, verify that list is still sorted
   */
  asrt_bp = *FREE_LIST_p;
  int num_chunks_ins = 0;
  last_size = 0;
#if IS_DEBUG
  __printfl(*FREE_LIST_p);
#endif
  while (asrt_bp != NULL) {
    assert(GET_SIZE(HDRP(asrt_bp)) >= last_size);
    num_chunks_ins++;
    asrt_bp = NEXT_FL_BLKP(asrt_bp);
  }
  assert(num_chunks_ins == num_chunks+1);
#endif

  return bp;
}

static void *__chk_splice(void *bp, size_t asize) {
  /*
   * Steps:
   * (1) Initialize both chunks: header & footer
   * (2) Reinsert Part_2 into free list
   *      - If chunk is too small, leave it.
   * (3) Return Part_1
   */
  size_t PART_1_size = asize;
  size_t PART_2_size = GET_SIZE(HDRP(bp)) - asize;

#if IS_ASSERT
  assert(IS_DWORD_ALIGNED(PART_2_size));
  assert(IS_DWORD_ALIGNED(PART_1_size));
  assert(PART_2_size >= MIN_CHK_SIZE);
#endif

  void *p1bp, *p2bp;

  p1bp = bp;
  PUT(HDRP(p1bp), PACK(PART_1_size, 0));
  PUT(FTRP(p1bp), PACK(PART_1_size, 0));

  p2bp = NEXT_BLKP(p1bp);
  PUT(HDRP(p2bp), PACK(PART_2_size, 0));
  PUT(FTRP(p2bp), PACK(PART_2_size, 0));

#if IS_ASSERT
  assert(GET_SIZE(HDRP(p1bp)) == GET_SIZE(FTRP(p1bp)));
  assert(GET_SIZE(HDRP(p2bp)) == GET_SIZE(FTRP(p2bp)));
  assert(GET_ALLOC(HDRP(p1bp)) == GET_ALLOC(FTRP(p1bp)));
  assert(GET_ALLOC(HDRP(p2bp)) == GET_ALLOC(FTRP(p2bp)));
#endif

#if IS_DEBUG
  PRINT_CHK("CHUNK_SPLICE - PART_1: ", p1bp);
  PRINT_CHK("CHUNK_SPLICE - PART_2: ", p2bp);
#endif
  size_t bin_ind = __find_fit_ind(PART_2_size);
  __flinsert(p2bp, &FAST_LOG_BIN[bin_ind]);

  return p1bp;
}

static void *__extend_heap(size_t asize)
{
#if IS_ASSERT
#if IS_DEBUG
  printf("EXTENDING HEAP - SIZE=%d\n", (int)asize);
#endif
  assert(IS_DWORD_ALIGNED(asize));
#endif

  void *bp = mem_sbrk(asize);
  /*
   * Initialize this block for sake of correctness. The real
   * purpose is to use the defined macros to move the epilogue
   * to the new end of heap.
   */
  PUT(HDRP(bp), PACK(asize, 0));
  PUT(FTRP(bp), PACK(asize, 0));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // move epilogue

  return bp;
 }

/*
 * FIXME: This will run slowly, assuming each Free List sorts
 *        by address order. Fix it if performance is a problem.
 */
static void *__extend_and_prealloc(size_t bin_ind, size_t asize)
{
  /*
   * Steps:
   * (1) Get from OS - sbrk(asize * NUM_PREALLOC size)
   * (2) Split and initialize chunks
   * (3) If alloc'd more than 1 chunk, reinsert all but the first
   *     to free list
   * (4) Return bp of first chunk
   */
#if IS_ASSERT
#if IS_DEBUG
  printf("EXTENDING HEAP - SIZE=%d, for FREE_LIST[%d]\n",
    (int)asize, (int)bin_ind);
#endif
  assert(IS_DWORD_ALIGNED(asize));
#endif

  void *bp;
  int num_allocd;
  if (bin_ind < NUM_FAST_BIN) {
    num_allocd = NUM_CHK_PREALLOC;

    if (asize == 144)
      num_allocd = NUM_CHK_PREALLOC_ADV;

    if ((bp = mem_sbrk(asize*num_allocd)) == (void *)-1)
      return NULL;
  } else {
    num_allocd = 1;
    if ((bp = mem_sbrk(asize)) == (void *)-1)
      return NULL;
  }

#if IS_ASSERT
  assert(IS_DWORD_ALIGNED(bp));
  assert(IS_DWORD_ALIGNED(asize));
#endif

  /*
   * Initialize this block for sake of correctness. The real
   * purpose is to use the defined macros to move the epilogue
   * to the new end of heap.
   */
  size_t size_allocd = num_allocd * asize;
  PUT(HDRP(bp), PACK(size_allocd, 0));
  PUT(FTRP(bp), PACK(size_allocd, 0));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // move epilogue

  int i;
  void *bp_ctr = bp;
  for (i = 0; i < num_allocd; i++, bp_ctr = NEXT_BLKP(bp_ctr)) {
    /* Initialize chunk */
    PUT(HDRP(bp_ctr), PACK(asize, 0));
    PUT(FTRP(bp_ctr), PACK(asize, 0));

#if IS_ASSERT
    assert(NEXT_BLKP(bp_ctr) == bp_ctr+asize);
#endif

    /* (3) Insert rest of blocks to free list */
    if (i != 0) {
      assert(__flinsert(bp_ctr, &FAST_LOG_BIN[bin_ind]));
    }
  }

  return bp;
}

static void *__free_list_alloc(void **FREE_LIST_p, size_t asize) {
  /*
   * Steps:
   * (1) Traverse the list until find a chunk of size that is the
   *     exact or closest fit. Assume that chunks are sorted in
   *     ascending size order s.t. size of the previous chunk is
   *     always be less than size of the current chunk.
   * (2) If exact fit:
   *      - Remove chunk from FREE_LIST
   *      - place() chunk
   * (3) If not exact fit:
   *      - Split into 2 parts (Part_1, and Part_2)
   */
#if IS_DEBUG
  assert(FREE_LIST_p != NULL);
  __printfl(*FREE_LIST_p);
#endif

  if (*FREE_LIST_p == NULL)
    return NULL;

  // this is the block we want to free
  void *bp = *FREE_LIST_p;
  size_t chunk_size;
  for (; bp != NULL; bp = NEXT_FL_BLKP(bp)) {
    chunk_size = GET_SIZE(HDRP(bp));
#if IS_ASSERT
#if IS_DEBUG
    PRINT_CHK("FREE_LIST_ALLOC: ", bp);
#endif
    assert(IS_DWORD_ALIGNED(chunk_size));
    assert(!GET_ALLOC(HDRP(bp)));
#endif

    if (asize <= chunk_size) {
      // found one
      if (asize == chunk_size) {
        assert(__flremove(bp, FREE_LIST_p));
        return bp;
      }

      if ((chunk_size-asize) >= MIN_CHK_SIZE) {
        /*
         * Split chunk into 2 parts, do not use this chunk
         * if leftover is less than MIN_CHK_SIZE
         */
        assert(__flremove(bp, FREE_LIST_p));
        return __chk_splice(bp, asize);
      } else if (chunk_size > MIN_CHK_SIZE &&
        (chunk_size-asize) < (chunk_size>>3)) {
        assert(__flremove(bp, FREE_LIST_p));
        return bp;
      }
    }
  }

  return NULL;
}

static size_t __find_fit_ind(size_t asize) {
  size_t bin_ind;
  if (asize < (DSIZE * (NUM_FAST_BIN+2))) {
    /* fast bin allocation */
    bin_ind = (asize / DSIZE) - 2;
  } else {
    /* log bin allocation */
    bin_ind = NUM_FAST_BIN; // start of LOG_BIN
    int pow = LEAST_LOG_BIN_POW;

    while (bin_ind < MAX_BIN_SIZE-1) {
      if (bin_ind == NUM_FAST_BIN) {
        if (asize >= (DSIZE * (NUM_FAST_BIN+2)) &&
            asize <= POW_2(pow)) {
          break;
        }
        pow++;
        bin_ind++;
        continue;
      }

      if (asize >= (POW_2(pow-1))+DSIZE &&
          asize <= POW_2(pow)) {
        break;
      }

      pow++;
      bin_ind++;
    }
  }

#if IS_ASSERT
#if IS_DEBUG
  printf("ASIZE=%d -> FREE_LIST[%d]\n", (int)asize, (int)bin_ind);
#endif
  assert(bin_ind < MAX_BIN_SIZE);
#endif

  return bin_ind;
}
/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void *find_fit(size_t asize)
{
  /*
   * Steps:
   * (1) Determine FAST_LOG_BIN index for asize
   * (2) Continue searching until a chunk is found
   * (3) If no chunks are found, extend heap, do preallocation
   */
  void *bp = NULL;
  size_t bin_ind = __find_fit_ind(asize);

  size_t bin_ind_ctr = bin_ind;
  for (; bin_ind_ctr < MAX_BIN_SIZE; bin_ind_ctr++) {
#if IS_DEBUG
    printf("FIND_FIT: FREE_LIST[%d]\n", (int)bin_ind_ctr);
#endif
    if ((bp = __free_list_alloc(&FAST_LOG_BIN[bin_ind_ctr], asize)) != NULL) {
#if IS_ASSERT
      assert(IS_DWORD_ALIGNED(bp));
#endif
      return bp;
    }
  }

#if IS_ASSERT
  assert(bp == NULL);
#endif
  if ((bp = __extend_and_prealloc(bin_ind, asize)) != NULL) {
    return bp;
  }

  /* out of memory */
  return NULL;
}


/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp)
{
#ifdef STAT_REPORT_INTERVAL
  num_requests++;
  if (num_requests == STAT_REPORT_INTERVAL) {
    mm_check();
    num_requests = 0;
  }
#endif
#if IS_DEBUG
  printf("\n--------------------------FREE--------------------------\n");
  PRINT_CHK("CHK TO FREE: ", bp);
  printf("----------------------------------------------------------\n");
  __printheap();
  Mem_usr_allocd-=GET_SIZE(HDRP(bp));
#endif
  if(bp == NULL)
    return;

  size_t size = GET_SIZE(HDRP(bp));
  PUT(HDRP(bp), PACK(size,0));
  PUT(FTRP(bp), PACK(size,0));
  bp = __chk_coalesce(bp);
  assert(__flinsert(bp, &FAST_LOG_BIN[__find_fit_ind(GET_SIZE(HDRP(bp)))]));

#if IS_DEBUG
  printf("+++++++++++++++++++++++FINISHED FREE++++++++++++++++++++++\n");
#endif
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 *
 * (1) Put padding according to 2-word alignment
 **********************************************************/
void *mm_malloc(size_t size)
{
  /*
   * Steps:
   * (1) Determine adjusted size from request size - account for
   *     alignment padding, header & footer overhead
   */
#ifdef STAT_REPORT_INTERVAL
  num_requests++;
  if (num_requests == STAT_REPORT_INTERVAL) {
    mm_check();
    num_requests = 0;
  }
#endif

  /* Ignore spurious requests */
  if (size == 0)
    return NULL;

  char * bp;

  size_t asize; /* adjusted block size */
  if (size <= DSIZE)
      asize = 2 * DSIZE;
  else
      asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

#if IS_DEBUG
  printf("\n------------MALLOC: %d Bytes -> %d Byte CHK-------------\n",
    (int)size, (int)asize);
  printf("----------------------------------------------------------\n");
  if (size == 16) num_16_byte_requests++;
  __printheap();
  Mem_usr_allocd+=size;
  P_k = Mem_usr_allocd > P_k ? Mem_usr_allocd : P_k;
#endif
#if IS_ASSERT
  assert(IS_DWORD_ALIGNED(asize));
#endif

  /* Search the free list for a fit */
  if ((bp = find_fit(asize)) != NULL) {
    size_t bsize = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(bsize, 1));
    PUT(FTRP(bp), PACK(bsize, 1));

#if IS_DEBUG
    printf("+++++++++++++++++++++++FINISHED MALLOC++++++++++++++++++++\n");
    PRINT_CHK("CHK ALLOCD: ", bp);
    printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
#endif
    return bp;
  }

  return NULL;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
#ifdef STAT_REPORT_INTERVAL
  num_requests++;
  if (num_requests == STAT_REPORT_INTERVAL) {
    mm_check();
    num_requests = 0;
  }
#endif
#if IS_DEBUG
  printf("\n------------------REALLOC: %d -> %d Bytes---------------\n",
    (int)GET_SIZE(HDRP(ptr)), (int)size);
  PRINT_CHK("CHK TO REALLOC: ", ptr);
  printf("----------------------------------------------------------\n");
  __printheap();
#endif
  /* If size == 0 then this is just free, and we return NULL. */
  if(size == 0){
    mm_free(ptr);
    return NULL;
  }
  /* If oldptr is NULL, then this is just malloc. */
  if (ptr == NULL)
    return (mm_malloc(size));

  size_t asize, curr_asize;
  if (size <= DSIZE)
    asize = 2 * DSIZE;
  else
    asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

  /* Alloc'd block is large enough for new size */
  curr_asize = GET_SIZE(HDRP(ptr));

#if IS_ASSERT
  assert(IS_DWORD_ALIGNED(asize));
  assert(IS_DWORD_ALIGNED(curr_asize));
#endif

  void *bp; // New chunk (if necessary)
  size_t diff;
  if (asize == curr_asize) {
    bp = ptr;
    goto end;
  }
  /* New size is smaller than block size, no need to malloc */
  else if (asize < curr_asize) {
#if IS_DEBUG
    Mem_usr_allocd -= (curr_asize-size);
#endif
#if !OPT_REALLOC
    if ((curr_asize - asize) >= MIN_CHK_SIZE) {
      bp = __chk_splice(ptr, asize);
      size_t bsize = GET_SIZE(HDRP(bp));
      PUT(HDRP(bp), PACK(bsize, 1));
      PUT(FTRP(bp), PACK(bsize, 1));

      goto end;
    }
#else
    diff = curr_asize - asize;
    if (diff >= (curr_asize>>1) && diff >= MIN_LOG_CHK_SIZE) {
      bp = __chk_splice(ptr, asize);
      size_t bsize = GET_SIZE(HDRP(bp));
      PUT(HDRP(bp), PACK(bsize, 1));
      PUT(FTRP(bp), PACK(bsize, 1));
    }
#endif

    /* Leftover size is below min size threshold of free list*/
    bp = ptr;
    goto end;
  }
  /* asize > curr_asize */
  else {
#if IS_DEBUG
    printf("REALLOC->MALLOC, %d-%d=%d\n",
      (int)asize, (int)curr_asize,(int)(asize - curr_asize));
#endif
    /*
     * Coalesce now, maybe there are free blocks nearby
     * This is a 2-pronged optimization:
     * (1) Utilization: if coalesced size is large enough
     *     for new size, then don't have to extend heap
     * (2) Time: malloc+free is needed -> expensive (time-wise)
     */
    bp = __chk_coalesce(ptr);
    size_t coalesced_sz = GET_SIZE(HDRP(bp));
    if (coalesced_sz >= asize) {
      if (bp != ptr) {
#if IS_DEBUG
        PRINT_CHK("Copying from old to new chunk: ", bp);
#endif
        memcpy(bp, ptr, curr_asize);
      }
#if IS_DEBUG
      Mem_usr_allocd += (size - curr_asize);
      P_k = Mem_usr_allocd > P_k ? Mem_usr_allocd : P_k;
#endif
      diff = coalesced_sz - asize;
      if (diff >= (coalesced_sz>>1) && diff >= MIN_LOG_CHK_SIZE) {
        bp = __chk_splice(bp, asize);
        coalesced_sz = asize;
      }
      PUT(HDRP(bp), PACK(coalesced_sz, 1));
      PUT(FTRP(bp), PACK(coalesced_sz, 1));
      goto end;
    }

    /* If we can't use this, return it to Free List */
    if (coalesced_sz > curr_asize) {
      assert(__flinsert(bp, &FAST_LOG_BIN[__find_fit_ind(coalesced_sz)]));
    }

    /*
     * Last ditch effort to stop from malloc:
     * - do this only for large chks
     * - see if we're at the end-of-heap
     * - if we are, then extend the heap and allocate an amount
     *   that is much less 'size'
     *
     * For programs with many reallocs, it would be extremely
     * beneficial to keep the large blocks which are often
     * reallocd near the end of the heap so they can be easily
     * extended.
     */
    if (GET_SIZE(HDRP(NEXT_BLKP(ptr))) == 0 &&
        asize > POW_2(18))
    { // check for epilogue
      bp = __extend_heap(DSIZE * (((asize>>4) + (DSIZE-1)) / DSIZE));
      size_t extended_size = GET_SIZE(HDRP(bp)) + curr_asize;
      PUT(HDRP(ptr), PACK(extended_size, 1));
      PUT(FTRP(ptr), PACK(extended_size, 1));
      bp = ptr;
      goto end;
    }

    /* Nothing we can do, call malloc.. */
    bp = mm_malloc(size);
    if (bp == NULL)
      return NULL;

    memcpy(bp, ptr, curr_asize);
    mm_free(ptr);
  }

  end:
#if IS_DEBUG
  printf("++++++++++++++++++++FINISHED REALLOC++++++++++++++++++++++\n");
  PRINT_CHK("NEW CHK: ", bp);
  printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
#endif
  return bp;
}

static void __printfl(void *FREE_LIST)
{
#if IS_DEBUG
  if (FREE_LIST == NULL) {
    printf("FREE_LIST is empty..\n");
    return;
  }

  void *bp = FREE_LIST;
  size_t flsize = 0;
  size_t size = __find_fit_ind(GET_SIZE(HDRP(FREE_LIST)));
  printf("FREE_LIST[%d]: ", (int)size);
  while (bp != NULL) {
    /* PRINT_CHK but without newline */
    if (flsize < 50) {
      printf("<%d|%d, 0x%x>", (int) GET_SIZE(HDRP(bp)),
             (int) GET_ALLOC(HDRP(bp)), (unsigned int) bp);
    }
    flsize++;
    bp = NEXT_FL_BLKP(bp);
    if (bp != NULL && flsize < 50) {
      printf(", ");
    }
  }
  if (flsize > 50)
    printf("...");
  printf(" | FL_SIZE=%d\n", (int)flsize);
#endif
}

static void __printheap(void)
{
#if IS_DEBUG
  printf("\n=======================HEAP START=======================\n");
  size_t heap_size = mem_heap_hi()-mem_heap_lo();
  printf("%24s %*d bytes\n", "Total Heap Size:", 10, (int)heap_size);
  printf("%24s %*d bytes\n", "P_k:", 10, (int)P_k);
  printf("%24s %*d bytes\n", "Working Set:", 10, (int)Mem_usr_allocd);
  printf("%24s %*d bytes\n", "16-byte requests:", 10, num_16_byte_requests);
  printf("%24s %*.2lf %%\n", "Utilization:", 10,
         ((double)P_k/(double)heap_size)*100);
  printf("%24s %*.2lf %%\n", "16-byte vs Working Set:", 10,
         ((double)num_16_byte_requests*32/(double)Mem_usr_allocd)*100);
  printf("\n");

  void *_bp;
  _bp = (mem_heap_lo()+DSIZE);
  int count = 0;
  for (; GET_SIZE(HDRP(_bp)) > 0; _bp = NEXT_BLKP(_bp)) {
    count++;
  }

  if (count < 100) {
    _bp = (mem_heap_lo()+DSIZE);
    for (; GET_SIZE(HDRP(_bp)) > 0; _bp = NEXT_BLKP(_bp)) {
      printf("0x%x: ----HEADER: ALLOCD=%d | SIZE=%d----\n",
        (unsigned int) (HDRP(_bp)), (int) GET_ALLOC(HDRP(_bp)),
        (int) GET_SIZE(HDRP(_bp)));
      printf("0x%x: |         PAYLOAD=%*d       |\n",
        (unsigned int) _bp, 10, (int) (GET_SIZE(HDRP(_bp)) - DSIZE));
      printf("0x%x: ----FOOTER: ALLOCD=%d | SIZE=%d----\n",
        (unsigned int) (FTRP(_bp)), (int) GET_ALLOC(FTRP(_bp)),
        (int) GET_SIZE(FTRP(_bp)));
    }
  }
  printf("=======================HEAP END=======================\n\n");
#endif
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistent.
 *********************************************************/
static void mm_check(void)
{
#if IS_ASSERT
  /*
   * - Header & footer are the same
   * - Minimum chunk size is MIN_CHK_SIZE
   * - Count # chunks total in Free Lists is same as total # chunks
   *   by linear heap traversal
   * - Epilogue is always last
   * - Report on memory utilization
   */
  size_t CHK_STAT[MAX_BIN_SIZE];
  int i;
  for (i = 0; i < MAX_BIN_SIZE; i++) {
    CHK_STAT[i] = 0;
  }

  size_t NUM_TOTAL_CHK = 0;
  size_t NUM_FREE_CHK = 0;
  size_t NUM_TOTAL_ALLOC = 0;
  size_t NUM_TOTAL_FREE = 0;
  size_t chk_size;
  void *bp;
  for (bp = (mem_heap_lo() + DSIZE); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    chk_size = GET_SIZE(HDRP(bp));
    if (!GET_ALLOC(HDRP(bp))) {
      NUM_FREE_CHK++;
      CHK_STAT[__find_fit_ind(chk_size)]++;
      NUM_TOTAL_FREE += chk_size;
    }
    NUM_TOTAL_ALLOC+=chk_size;
    NUM_TOTAL_CHK++;
  }

  void *FREE_LIST;
  size_t NUM_CHK_FL = 0;
  for (i = 0; i < MAX_BIN_SIZE; i++) {
    FREE_LIST = FAST_LOG_BIN[i];

    void *chk_ptr = FREE_LIST;
    size_t num_chks_fl = 0;
    while (chk_ptr != NULL) {
      num_chks_fl++;
      NUM_CHK_FL++;
      chk_ptr = NEXT_FL_BLKP(chk_ptr);
    }

    assert(CHK_STAT[i] == num_chks_fl);
  }

  assert(NUM_FREE_CHK == NUM_CHK_FL);
  printf("\n===============REPORT=================\n");
  printf("NUM_TOTAL_CHKS=%d, NUM_FREE_CHKS=%d\n",
    (int)NUM_TOTAL_CHK, (int)NUM_FREE_CHK);
  printf("Bytes Allocated:\t%*d bytes\n", 10, (int)NUM_TOTAL_ALLOC);
  printf("Bytes Free:\t\t%*d bytes\n", 10, (int)NUM_TOTAL_FREE);
  printf("Ext. Fragmentation (%%):\t%*.2lf %%\n", 10,
    ((double)NUM_TOTAL_FREE/(double)NUM_TOTAL_ALLOC)*100);
  for (i = 0; i < MAX_BIN_SIZE; i++) {
    printf("FREE_LIST[%d]\t-> %d chks \t|", i, (int)CHK_STAT[i]);
    if ((i+1)%3 == 0) {
      printf("\n");
    }
  }
  printf("\n======================================\n");
#endif
}
