/*--------------------------------------------------------------------*/
/* heapmgr1.c                                                      */
/*--------------------------------------------------------------------*/

//Mental model first

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "chunk.h"  /* Provides Chunk_T and span-based header API */

#define FALSE 0
#define TRUE  1

/* Minimum number of *payload* units to request on heap growth.
 * (The actual request adds 1 header unit on top.) */
enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Head of the free list (ordered by ascending address). */
Chunk_T s_free_head = NULL;

/* Heap bounds: [s_heap_lo, s_heap_hi). */
void *s_heap_lo = NULL, *s_heap_hi = NULL;

int s_heap_booted = FALSE;

//Delete later --start
#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
  #define DBG(...)  do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
  #define DBG(...)  do { } while (0)
#endif

static inline const char* S(int st){ return st==CHUNK_FREE?"F":"U"; }

/* Print one block’s header/footer info */
static void dump_block(const char *tag, Chunk_T h) {
    if (!h) { DBG("%s: NULL\n", tag); return; }
    Chunk_FT f = get_footer_from_header(h);
    DBG("%s: H=%p span=%d st=%s F=%p fspan=%s\n",
        tag, (void*)h, chunk_get_span_units(h), S(chunk_get_status(h)),
        (void*)f, f ? (f->span==chunk_get_span_units(h)?"ok":"BAD") : "NULL");
}

/* Physical walk from s_heap_lo to s_heap_hi */
static void dump_physical(const char *tag) {
    DBG("\n-- PHYS %s  [lo=%p hi=%p] --\n", tag, s_heap_lo, s_heap_hi);
    for (Chunk_T w=(Chunk_T)s_heap_lo; w && w<(Chunk_T)s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        Chunk_FT f = get_footer_from_header(w);
        DBG("  %p: span=%d st=%s  F=%p\n",
            (void*)w, chunk_get_span_units(w), S(chunk_get_status(w)), (void*)f);
    }
    DBG("-- /PHYS --\n");
}

/* Free list walk from head following next; also verifies symmetry inline */
static void dump_freelist(const char *tag) {
    DBG("\n-- FL %s  head=%p --\n", tag, (void*)s_free_head);
    for (Chunk_T w=s_free_head; w; w=chunk_get_next_free(w)) {
        Chunk_T nx = chunk_get_next_free(w);
        Chunk_T pv = chunk_get_prev_free(w);
        DBG("  %p: span=%d st=%s  prev=%p next=%p\n",
            (void*)w, chunk_get_span_units(w), S(chunk_get_status(w)),
            (void*)pv, (void*)nx);
        if (nx) {
            if (chunk_get_prev_free(nx) != w)
                DBG("    !! asym: next->prev != me (next=%p prev_of_next=%p me=%p)\n",
                    (void*)nx, (void*)chunk_get_prev_free(nx), (void*)w);
        }
    }
    DBG("-- /FL --\n");
}

/* Call this around sensitive points */
static void dump_all(const char *tag) {
    dump_physical(tag);
    dump_freelist(tag);
}

//Delete later -- end

/*--------------------------------------------------------------------*/
/* check_heap_validity
 *
 * Lightweight integrity checks for the entire heap and the free list.
 * This is a *basic* sanity check. Passing it does not prove correctness
 * of all invariants, but it helps catch common structural bugs.
 *
 * Returns TRUE (1) on success, FALSE (0) on failure.
 *
 * Checks performed:
 *  - Heap bounds are initialized.
 *  - Every physical block within [s_heap_lo, s_heap_hi) passes chunk_is_valid.
 *  - Each node in the free list is marked CHUNK_FREE and passes chunk_is_valid.
 *  - Adjacency/Free-List consistency: if a free block's physical successor
 *    is exactly its next free block, then they should have been coalesced
 *    already (report as "uncoalesced").
 */
#ifndef NDEBUG
static int
check_heap_validity(void)
{
    Chunk_T w;
    
    //makes sure heap is initialized.
    if (s_heap_lo == NULL) { fprintf(stderr, "Uninitialized heap start\n"); return FALSE; }
    if (s_heap_hi == NULL) { fprintf(stderr, "Uninitialized heap end\n");   return FALSE; }

    if (s_heap_lo == s_heap_hi) {
        if (s_free_head == NULL) return TRUE; //if heap is empty, the free list should also be empty
        fprintf(stderr, "Inconsistent empty heap\n");
        return FALSE;
    }

    /* Walk all physical blocks in address order. and check if their chunk is valid*/
    for (w = (Chunk_T)s_heap_lo;
         w && w < (Chunk_T)s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
    }

    /* Walk the free list; ensure nodes are free and not trivially coalescible. */
    for (w = s_free_head; w; w = chunk_get_next_free(w)) {
        Chunk_T n;

        if(w == s_free_head && chunk_get_prev_free(w)) return FALSE;

        if (chunk_get_status(w) != CHUNK_FREE) { //check if there are any non-free chunks in the free list
            fprintf(stderr, "Non-free chunk in the free list\n");
            return FALSE;
        }
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE; 

        n = chunk_get_adjacent(w, s_heap_lo, s_heap_hi); // checks if any chunk was forgotten to be coalesced
        if (n != NULL && chunk_get_status(n) == CHUNK_FREE) {
            fprintf(stderr, "Uncoalesced adjacent free chunks\n");
            return FALSE;
        }

        //checking if next's prev_free is w - symmetry
        n = chunk_get_next_free(w);
        if( n && chunk_get_prev_free(n) != w) return FALSE;

        //checking if prev's next is w - symmetry
        n = chunk_get_prev_free(w);
        if(w != s_free_head && chunk_get_next_free(n) != w) return FALSE;
    }
    return TRUE;
}
#endif /* NDEBUG */

/*--------------------------------------------------------------------*/
/* bytes_to_payload_units
 *
 * Convert a byte count to the number of *payload* units, rounding up
 * to the nearest multiple of CHUNK_UNIT. The result does not include
 * the header unit. */
static size_t
bytes_to_payload_units(size_t bytes)
{
    return (bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
}

/*--------------------------------------------------------------------*/
/* header_from_payload
 *
 * Map a client data pointer back to its block header pointer by
 * stepping one header unit backward. */
static Chunk_T header_from_payload(void *p)
{
    return (Chunk_T)((char *)p - CHUNK_UNIT);
}

/*--------------------------------------------------------------------*/
/* heap_bootstrap
 *
 * Initialize heap bounds using sbrk(0). Must be called exactly once
 * before any allocation occurs. Exits the process on fatal failure. */
static void heap_bootstrap(void)
{
    if (s_heap_booted) return;
    s_heap_lo = s_heap_hi = sbrk(0);
    if (s_heap_lo == (void *)-1) {
        fprintf(stderr, "sbrk(0) failed\n");
        exit(-1);
    }
    s_heap_booted = TRUE;
}

/*--------------------------------------------------------------------*/
/* coalesce_two
 *
 * Given two *adjacent* free blocks a and b (a < b), merge them into a.
 * The new span of 'a' becomes span(a) + span(b). The free-list link of
 * 'a' is updated to skip 'b'. Returns the merged block 'a'. */
static Chunk_T coalesce_two(Chunk_T a, Chunk_T b)
{
    //DELETE LATER --start
    DBG("\n>> coalesce_two a=%p b=%p\n", (void*)a, (void*)b);
    dump_block("a.before", a);
    dump_block("b.before", b);
    //DELETE LATER --end

    if(b < a){ //switch order if b < a
        Chunk_T temp = a;
        a = b;
        b = temp;
    }

    Chunk_T b_next_free = chunk_get_next_free(b);

    // ----- All checks ------
    assert(a != NULL);
    assert(b != NULL);
    assert(a != b);
    assert(s_heap_lo != NULL && s_heap_hi != NULL);

    assert((void*)a >= s_heap_lo && (void*)a < s_heap_hi);
    assert((void*)b >= s_heap_lo && (void*)b < s_heap_hi);

    assert(chunk_get_span_units(a) >= 2);
    assert(chunk_get_span_units(b) >= 2);

    assert(chunk_get_adjacent(a, s_heap_lo, s_heap_hi) == b);
    assert(chunk_get_prev_adjacent(b, s_heap_lo, s_heap_hi) == a);

    assert(chunk_is_valid(a, s_heap_lo, s_heap_hi));
    assert(chunk_is_valid(b, s_heap_lo, s_heap_hi));

    assert(chunk_get_status(a) == CHUNK_FREE);
    assert(chunk_get_status(b) == CHUNK_FREE);

    if (chunk_get_prev_free(b) != NULL)
        assert(chunk_get_next_free(chunk_get_prev_free(b)) == b);
    if (chunk_get_next_free(a) != NULL)
        assert(chunk_get_prev_free(chunk_get_next_free(a)) == a);

    if (b_next_free != NULL) {
        assert(chunk_is_valid(b_next_free, s_heap_lo, s_heap_hi));
        assert(chunk_get_status(b_next_free) == CHUNK_FREE);
    }

    Chunk_FT a_footer = get_footer_from_header(a);
    Chunk_FT b_footer = get_footer_from_header(b);
    assert(a_footer != NULL && b_footer != NULL);
    assert((void*)a_footer < (void*)b_footer);
    assert((void*)b_footer <= s_heap_hi);

    // ------ Actual code ------

    chunk_set_span_units(a, chunk_get_span_units(a) + chunk_get_span_units(b));
    Chunk_T a_prev_free = chunk_get_prev_free(a);

    if (b == s_free_head) {
        s_free_head = a;
        chunk_set_prev_free(a, NULL);   /* head must have prev == NULL */
    }

    if(chunk_get_next_free(a) != b || chunk_get_prev_free(b) != a){ //if there are other blocks in between a and b in the free list
        Chunk_T b_btwn = chunk_get_prev_free(b); //block before b in free list

        if (b_btwn) {
            chunk_set_next_free(b_btwn, b_next_free);
        } else {
            /* b was head (or treated as such) → ensure survivor is head */
            assert(s_free_head == a);
            assert(chunk_get_prev_free(a) == NULL);
        }

        if (b_next_free) chunk_set_prev_free(b_next_free, b_btwn ? b_btwn : a);
    } else {
        chunk_set_prev_free(a, a_prev_free);
        chunk_set_next_free(a, chunk_get_next_free(b));
        if(b_next_free)     chunk_set_prev_free(b_next_free, a);
    }

    //DELETE LATER --start
    DBG("<< coalesce_two -> %p\n", (void*)a);
    dump_block("a.after", a);

    return a;
}

/*--------------------------------------------------------------------*/
/* split_for_alloc
 *
 * Split a free block 'c' into:
 *   - a smaller *leading* free block of span 'remain_span'
 *   - a *trailing* allocated block 'alloc' of span 'alloc_span'
 *
 * Inputs:
 *   c          : free block to split
 *   need_units : required *payload* units (does not include header)
 *
 * Computations:
 *   alloc_span  = 1 (header) + need_units
 *   remain_span = span(c) - alloc_span
 *
 * Pre-conditions:
 *   remain_span > 0 (i.e., the split leaves a positive-size free block).
 *
 * Returns the header pointer of the newly created allocated block. */
static Chunk_T split_for_alloc(Chunk_T c, size_t need_units)
{
    //DELETE LATER -- start
    DBG("\n>> split_for_alloc c=%p need=%zu\n", (void*)c, need_units);
    dump_block("c.before", c);
    //DELETE LATER -- end

    /* --- added checks: basic preconditions --- */
    assert(c != NULL);
    assert((long)need_units >= 0);

    Chunk_T alloc;
    Chunk_T c_prev = chunk_get_prev_free(c);

    int old_span    = chunk_get_span_units(c);
    int alloc_span  = (int)(2 + need_units);
    int remain_span = old_span - alloc_span;

    /* --- added checks: span math & status/bounds --- */
    assert(old_span >= 2);        /* original free block must be ≥ H+F */
    assert(alloc_span >= 2);      /* allocated block must be ≥ H+F */
    assert(remain_span >= 2);     /* remaining free block must be ≥ H+F */

    assert(c >= (Chunk_T)s_heap_lo && c <= (Chunk_T)s_heap_hi);
    assert(chunk_get_status(c) == CHUNK_FREE);
    assert(remain_span >= 2);

    /* --- added checks: if c has a prev in the DLL, it must point to c --- */
    if (c_prev != NULL) {
        assert(chunk_get_status(c_prev) == CHUNK_FREE);
        assert(chunk_get_next_free(c_prev) == c);
    } else {
        /* if no prev, c should be the head */
        assert(c == s_free_head);
    }

    /* Shrink the leading free block. */
    chunk_set_span_units(c, remain_span);

    /* --- added checks: c is still valid after shrinking --- */
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(c) == CHUNK_FREE);

    /* The allocated block begins immediately after the smaller free block. */
    alloc = chunk_get_adjacent(c, s_heap_lo, s_heap_hi); //alloc is now the 2nd part of the block

    /* --- added checks: alloc header address sanity --- */
    assert(alloc != NULL);
    assert((void*)alloc >= s_heap_lo && (void*)alloc < s_heap_hi);

    chunk_set_span_units(alloc, alloc_span);
    chunk_set_status(alloc, CHUNK_USED);
    /* --- added checks: alloc validity after initializing span/status --- */
    assert(chunk_is_valid(alloc, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(alloc) == CHUNK_USED);

    // chunk_set_next_free(alloc, chunk_get_next_free(c));  /* harmless for allocated */

    /* --- added checks: DLL successor (if any) remains a FREE block --- */
    {
        Chunk_T c_succ = chunk_get_next_free(c);
        if (c_succ != NULL) {
            assert(chunk_get_status(c_succ) == CHUNK_FREE);
        }
    }

    Chunk_FT footer = (Chunk_FT)get_footer_from_header(c);
    /* --- added checks: footer computed for c lies within heap and after c --- */
    assert(footer != NULL);
    assert((void*)footer > (void*)c);
    assert((void*)footer <= s_heap_hi);

    chunk_set_prev_free(c, c_prev);
    // chunk_set_prev_free(alloc, c);

    /* --- added checks: final adjacency and block validity --- */
    assert(chunk_get_adjacent(c, s_heap_lo, s_heap_hi) == alloc);
    {
        Chunk_T after_alloc = chunk_get_adjacent(alloc, s_heap_lo, s_heap_hi);
        assert(after_alloc == NULL || (void*)after_alloc <= s_heap_hi);
    }
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_is_valid(alloc, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(c) == CHUNK_FREE);
    assert(chunk_get_status(alloc) == CHUNK_USED);

    //DELETE LATER --start
    DBG("<< split_for_alloc  rem(c)=%p alloc=%p\n", (void*)c, (void*)alloc);
    dump_block("rem.after", c);
    dump_block("alloc.after", alloc);

    return alloc;
}

/*--------------------------------------------------------------------*/
/* freelist_push_front
 *
 * Insert a block 'c' at the head of the free list (address-ordered).
 * If the new head is physically adjacent to the previous head,
 * coalesce them immediately to reduce fragmentation. */
static void freelist_push_front(Chunk_T c)
{
    //DELETE LATER --start
    DBG("\n>> push_front c=%p\n", (void*)c);
    dump_block("c.before", c);
    //DELETE LATER --end

    assert(c != NULL);
    assert(s_heap_lo != NULL && s_heap_hi != NULL);
    assert((void*)c >= s_heap_lo && (void*)c < s_heap_hi);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_span_units(c) >= 2);

    chunk_set_status(c, CHUNK_FREE); //set c as free chunk

    if (s_free_head == NULL) { //if it's an empty list
        s_free_head = c;
        chunk_set_next_free(c, NULL);
        chunk_set_prev_free(c, NULL);
        assert(chunk_get_prev_free(s_free_head) == NULL);
    }
    else { //if not an empty list,
        assert((void*)s_free_head >= s_heap_lo && (void*)s_free_head < s_heap_hi);
        assert(chunk_is_valid(s_free_head, s_heap_lo, s_heap_hi));
        assert(chunk_get_status(s_free_head) == CHUNK_FREE);

        //assert(c < s_free_head);
        Chunk_T c_next = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
        if (c_next && chunk_get_status(c_next) == CHUNK_FREE){ //coalesce if adjacent next is also free
            assert(chunk_is_valid(c_next, s_heap_lo, s_heap_hi));
            c = coalesce_two(c, c_next);
        }
        assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
        assert(chunk_get_status(c) == CHUNK_FREE);

        Chunk_T c_prev = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi); //coalesce if adjacent prev is also free
        if (c_prev && chunk_get_status(c_prev) == CHUNK_FREE){
            assert(chunk_is_valid(c_prev, s_heap_lo, s_heap_hi));
            assert(chunk_get_adjacent(c_prev, s_heap_lo, s_heap_hi) == c);
            c = coalesce_two(c_prev, c);
            assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
            assert(chunk_get_status(c) == CHUNK_FREE);
        }

        if (s_free_head != c) {                 //c might be head after coalesce
            chunk_set_next_free(c, s_free_head);
            assert(chunk_is_valid(s_free_head, s_heap_lo, s_heap_hi));
            assert(chunk_get_status(s_free_head) == CHUNK_FREE);
            chunk_set_prev_free(s_free_head, c);
        } else {
            //if we’re reusing the same head, its next/prev are already fine
            chunk_set_next_free(c, chunk_get_next_free(s_free_head));
        }
        
        chunk_set_prev_free(c, NULL);
        s_free_head = c;

        assert(s_free_head == c);
        assert(chunk_get_prev_free(s_free_head) == NULL);
        /* if next exists, its prev must be head */
        if (chunk_get_next_free(s_free_head) != NULL)
            assert(chunk_get_prev_free(chunk_get_next_free(s_free_head)) == s_free_head);
    }

    //DELETE LATER
    DBG("<< push_front head=%p\n", (void*)s_free_head);
    dump_freelist("after push_front");
}

/*--------------------------------------------------------------------*/
/* freelist_insert_after
 *
 * Insert block 'c' into the free list *after* node 'e'. Then, if
 * 'c' is physically adjacent to its neighbors ('e' or the physical
 * successor), coalesce accordingly. Returns the final (possibly merged)
 * block that occupies 'c''s position. */

// static Chunk_T
// freelist_insert_after(Chunk_T e, Chunk_T c) //only need one param
// {
//     Chunk_T n;

//     assert(e < c);
//     assert(chunk_get_status(e) == CHUNK_FREE);
//     assert(chunk_get_status(c) != CHUNK_FREE);

//     chunk_set_next_free(c, chunk_get_next_free(e));
//     chunk_set_next_free(e, c);
//     chunk_set_status(c, CHUNK_FREE);

//     chunk_set_prev_free(chunk_get_next_free(e), c);
//     chunk_set_prev_free(c, e);

//     /* Merge with lower neighbor if adjacent. */
//     if (chunk_get_adjacent(e, s_heap_lo, s_heap_hi) == c)
//         c = coalesce_two(e, c);

//     /* Merge with upper neighbor if that one is free and adjacent. */
//     n = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
//     if (n != NULL && chunk_get_status(n) == CHUNK_FREE)
//         c = coalesce_two(c, n);

//     return c;
// }

/*--------------------------------------------------------------------*/
/* freelist_detach
 *
 * Remove block 'c' from the free list. If 'c' is the head, 'prev'
 * must be NULL; otherwise 'prev' must precede 'c' in the list.
 * The block is marked as CHUNK_USED afterwards. */
static void freelist_detach(Chunk_T c) //only need one param
{
    //DELETE LATER --start
    DBG("\n>> detach c=%p\n", (void*)c);
    dump_block("c.before", c);
    //DELETE LATER --end

    Chunk_T prev = chunk_get_prev_free(c);
    Chunk_T next = chunk_get_next_free(c);

    assert(chunk_get_status(c) == CHUNK_FREE);

    if (prev == NULL){
        s_free_head = next;
        if (s_free_head != NULL) {
            chunk_set_prev_free(s_free_head, NULL);
        }
    }
    else
        chunk_set_next_free(prev, chunk_get_next_free(c));
    
    if(next) {
        chunk_set_prev_free(next, prev);
    }

    if (prev != NULL) assert(chunk_get_next_free(prev) == next);
    if (next != NULL) assert(chunk_get_prev_free(next) == prev);
    if (s_free_head != NULL) assert(chunk_get_prev_free(s_free_head) == NULL);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    chunk_set_status(c, CHUNK_USED);

    //DELETE LATER
    DBG("<< detach head=%p\n", (void*)s_free_head);
    dump_freelist("after detach");
}

/*--------------------------------------------------------------------*/
/* sys_grow_and_link
 *
 * Request more memory from the system via sbrk() and link the new
 * block into the free list (coalescing when possible).
 *
 * Inputs:
 *   prev       : last node in the free list (or NULL if list is empty)
 *   need_units : required *payload* units
 *
 * Actions:
 *   - Compute grow_span = 2 + max(need_units, SYS_MIN_ALLOC_UNITS).
 *   - sbrk(grow_span * CHUNK_UNIT) to obtain one big block.
 *   - Temporarily mark it USED (to avoid free-list invariants while
 *     inserting) and then insert/merge into the free list. */
static Chunk_T sys_grow_and_link(size_t need_units)
{
    //DELETE LATER -- one line
    DBG("\n>> grow need_units=%zu\n", need_units);

    Chunk_T c;
    size_t grow_data = (need_units < SYS_MIN_ALLOC_UNITS) ? SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = 2 + grow_data;  /* header + payload + footer units */

    c = (Chunk_T)sbrk(grow_span * CHUNK_UNIT);
    // if (c == (Chunk_T)-1)
    //     return NULL;

    if ((void*)c == (void*)-1)   return NULL;

    s_heap_hi = sbrk(0);

    chunk_set_span_units(c, (int)grow_span);
    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    chunk_set_status(c, CHUNK_FREE);   /* will fsslip to FREE once inserted */

    Chunk_T c_prev_adj = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);

    //DELETE LATER --start
    DBG("   grow got c=%p span=%zu  lo=%p hi=%p\n",
        (void*)c, grow_span, s_heap_lo, s_heap_hi);
    dump_block("grown", c);
    //DELETE LATER --end

    if(c_prev_adj && chunk_is_valid(c_prev_adj, s_heap_lo, s_heap_hi) && chunk_get_status(c_prev_adj) == CHUNK_FREE){
        c = coalesce_two(c_prev_adj, c);
        assert(check_heap_validity());
        return c;
    } 
    freelist_push_front(c);
    assert(check_heap_validity());

    //DELETE LATER -- one line
    dump_all("after grow");

    return c;
}

/*--------------------------------------------------------------------*/
/* heapmgr_malloc
 *
 * Allocate a block capable of holding 'size' bytes. Zero bytes returns
 * NULL. The allocated region is *uninitialized*. Strategy:
 *  1) Convert 'size' to payload units (no header).
 *  2) First-fit search the free list for a block whose (span-2) >= need.
 *  3) If found:
 *       - split if larger than needed; otherwise detach as exact fit.
 *     Else:
 *       - grow the heap and repeat the same split/detach logic.
 *  4) Return the payload pointer (header + 1 unit). */
void * heapmgr_malloc(size_t size)
{
    // static int booted = FALSE; //tracks whether heap was initialized
    Chunk_T cur; //cur: walks free list, prev,prevprev: tracks prev nodes
    size_t need_units; //requested payload expressed in chunk units

    if (size == 0) 
        return NULL;

    //if (!booted) { 
        heap_bootstrap(); 
    //    booted = TRUE; 
    //} //capture the initial program break (sbrk(0)) and set heap bounds.
    // lazy initialisation (Don’t set up heap until the first call to heapmgr_malloc().)
    // sbrk(0) returns the current addr just past the end of the process’s data segment (the heap).

    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);
    // prevprev = NULL;
    // prev = NULL;

    //DELETE LATER 
    DBG("\n>> malloc size=%zu need_units=%zu\n", size, need_units);
    dump_all("malloc entry");

    /* First-fit scan: usable payload units = span - 2 (exclude header). */
    for (cur = s_free_head; cur != NULL; cur = chunk_get_next_free(cur)) { //Walk the free list from its head using first-fit.
        size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2; //For a free block at cur, compute usable payload
        //DELETE LATER
        DBG("  scan cur=%p span=%d payload=%zu need=%zu\n",
        (void*)cur, chunk_get_span_units(cur), cur_payload, need_units);
        if (cur_payload >= need_units) { //if larger or same than needed, split it for alloc
            if (cur_payload > need_units)
                cur = split_for_alloc(cur, need_units);
            else //if same size, detach it
                freelist_detach(cur);

            assert(check_heap_validity());
            return (void *)((char *)cur + CHUNK_UNIT); //return the payload pointer just past the header 
        }
    }

    //none of free blocks fit the needed size:
    /* Need to grow the heap. */
    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    //DELETE LATER --2 lines
    DBG("  after grow cur=%p\n", (void*)cur);
    dump_all("after grow in malloc");

    /* If the new block merged with 'prev', back up one step. */
    // if (cur == prev) prev = prevprev;

    /* Final split/detach on the grown block. */
    if ((size_t)chunk_get_span_units(cur) - 2 > need_units) //	If it’s bigger than needed, split and keep the remainder on the free list.
        cur = split_for_alloc(cur, need_units);
    else
        freelist_detach(cur); //Otherwise, detach it as an exact fit.

    assert(check_heap_validity());
    return (void *)((char *)cur + CHUNK_UNIT); //return the payload pointer (header + one unit).
}

/*--------------------------------------------------------------------*/
/* heapmgr_free
 *
 * Free a previously allocated block pointed to by 'p'. If 'p' is NULL,
 * do nothing. The pointer must have been returned by heapmgr_malloc().
 * Strategy:
 *  1) Validate heap structure (debug only).
 *  2) Map payload pointer to its header.
 *  3) Find the insertion point in the address-ordered free list.
 *  4) Insert the block and coalesce with adjacent free neighbors. */
void heapmgr_free(void *p)
{
    Chunk_T c;

    assert(s_heap_lo != NULL && s_heap_hi != NULL);                      // heap must be initialized
    assert((p == NULL) || ((void*)p >= s_heap_lo && (void*)p < s_heap_hi)); // pointer must lie inside heap range

    if (p == NULL)
        return;

    assert(check_heap_validity());

    c = header_from_payload(p);
    assert(c != NULL);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));                     // header must be a valid chunk
    assert(chunk_get_status(c) == CHUNK_USED);                           // cannot free an already-free block
    assert(chunk_get_next_free(c) == NULL && chunk_get_prev_free(c) == NULL); // must not already be in free list

    freelist_push_front(c);

    assert(chunk_get_status(c) == CHUNK_FREE);                           // block should now be marked FREE
    assert(check_heap_validity());
}
