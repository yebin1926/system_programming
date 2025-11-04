/*--------------------------------------------------------------------*/
/* heapmgr1.c                                                      */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "chunk.h"  /* Provides Chunk_T and span-based header API */

#define FALSE 0
#define TRUE  1

#define TINY_BYTES        1000
#define TINY_SPAN         ((int)(2 + bytes_to_payload_units(TINY_BYTES)))

/* Minimum number of *payload* units to request on heap growth.
 * (The actual request adds 1 header unit on top.) */
enum { SYS_MIN_ALLOC_UNITS = 1024 };

/* Head of the free list (ordered by ascending address). */
Chunk_T s_free_head = NULL;

/* Heap bounds: [s_heap_lo, s_heap_hi). */
void *s_heap_lo = NULL, *s_heap_hi = NULL;

int s_heap_booted = FALSE;

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


static size_t
bytes_to_payload_units(size_t bytes)
{
    return (bytes + (CHUNK_UNIT - 1)) / CHUNK_UNIT;
}


static Chunk_T header_from_payload(void *p)
{
    return (Chunk_T)((char *)p - CHUNK_UNIT);
}


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

// Unlink c from free list WITHOUT changing its status.
static void freelist_unlink(Chunk_T c)
{
    Chunk_T prev = chunk_get_prev_free(c);
    Chunk_T next = chunk_get_next_free(c);

    if (prev) {
        chunk_set_next_free(prev, next);
    } else {
        /* Only change head if 'c' is actually the head */
        if (s_free_head == c) s_free_head = next;
    }

    if (next)  chunk_set_prev_free(next, prev);

    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, NULL);
}


static Chunk_T coalesce_two(Chunk_T a, Chunk_T b)
{
    if (b < a) { Chunk_T t = a; a = b; b = t; }

    /* Sanity */
    assert(a && b && a != b);
    assert(chunk_get_status(a) == CHUNK_FREE);
    assert(chunk_get_status(b) == CHUNK_FREE);
    assert(chunk_get_adjacent(a, s_heap_lo, s_heap_hi) == b);
    assert(chunk_get_prev_adjacent(b, s_heap_lo, s_heap_hi) == a);

    /* If a->next is b, we'll stitch to b->next after unlink */
    Chunk_T an = chunk_get_next_free(a);
    Chunk_T bn = chunk_get_next_free(b);

    /* Remove b from free list first (no status change). */
    freelist_unlink(b);

    /* Grow 'a' */
    chunk_set_span_units(a, chunk_get_span_units(a) + chunk_get_span_units(b));

    /* Fix 'a'->next (if 'an' was b, switch to 'bn') */
    if (an == b) an = bn;
    chunk_set_next_free(a, an);
    if (an) chunk_set_prev_free(an, a);

    /* If 'a' has no prev, it must be head and its prev must be NULL. */
    if (chunk_get_prev_free(a) == NULL) {
        s_free_head = a;
        /* head must have NULL prev */
        assert(chunk_get_prev_free(s_free_head) == NULL);
    }

    return a;
}


static Chunk_T split_for_alloc(Chunk_T c, size_t need_units)
{
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
    chunk_set_next_free(alloc, NULL);
    chunk_set_prev_free(alloc, NULL);
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

    chunk_set_prev_free(c, c_prev);
    // chunk_set_prev_free(alloc, c);

    /* --- added checks: final adjacency and block validity --- */
    assert(chunk_get_adjacent(c, s_heap_lo, s_heap_hi) == alloc);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_is_valid(alloc, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(c) == CHUNK_FREE);
    assert(chunk_get_status(alloc) == CHUNK_USED);

    return alloc;
}


static void freelist_push_front(Chunk_T c)
{
    assert(c && s_heap_lo && s_heap_hi);
    assert((void*)c >= s_heap_lo && (void*)c < s_heap_hi);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_span_units(c) >= 2);

    /* 1) Mark FREE and insert at head */
    chunk_set_status(c, CHUNK_FREE);

    // Fast path for small blocks: push with no coalesce
    if (chunk_get_span_units(c) <= TINY_SPAN) {
        chunk_set_prev_free(c, NULL);
        chunk_set_next_free(c, s_free_head);
        if (s_free_head) chunk_set_prev_free(s_free_head, c);
        s_free_head = c;

        /* Keep the head invariant tight. */
        assert(s_free_head != NULL);
        assert(chunk_get_prev_free(s_free_head) == NULL);
        return;
    }

    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, s_free_head);
    if (s_free_head) chunk_set_prev_free(s_free_head, c);
    s_free_head = c;

    /* 2) Coalesce with physical NEXT first (survivor stays 'c') */
    {
        Chunk_T nxt = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
        if (nxt && chunk_get_status(nxt) == CHUNK_FREE) {
            c = coalesce_two(c, nxt); /* survivor is 'c' */
            /* after coalesce_two, if c is head, its prev is NULL already */
        }
    }
    {
        Chunk_T prv = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);
        if (prv && chunk_get_status(prv) == CHUNK_FREE) {
            Chunk_T old_next_head = chunk_get_next_free(c);
            (void)old_next_head; /* not needed, but clarifies the flow */

            /* Remove 'c' (the head) from the free list temporarily */
            freelist_unlink(c);

            /* Merge into 'prv' */
            Chunk_T survivor = coalesce_two(prv, c); /* survivor == prv */

            /* Head fix: if survivor has no prev, it's the head; else head
               remains whatever freelist_unlink() left as s_free_head. */
            if (chunk_get_prev_free(survivor) == NULL)
                s_free_head = survivor;
        }
    }

    assert(s_free_head != NULL);
    assert(chunk_get_prev_free(s_free_head) == NULL);
}


static void freelist_detach(Chunk_T c) //only need one param
{
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
}


static Chunk_T sys_grow_and_link(size_t need_units)
{
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

    if(c_prev_adj && chunk_is_valid(c_prev_adj, s_heap_lo, s_heap_hi) && chunk_get_status(c_prev_adj) == CHUNK_FREE){
        c = coalesce_two(c_prev_adj, c);
        assert(check_heap_validity());
        return c;
    } 
    freelist_push_front(c);
    assert(check_heap_validity());

    return c;
}


void * heapgr_malloc(size_t size)
{
    // static int booted = FALSE; //tracks whether heap was initialized
    Chunk_T cur; //cur: walks free list, prev,prevprev: tracks prev nodes
    size_t need_units; //requested payload expressed in chunk units

    if (size == 0) 
        return NULL;

    heap_bootstrap(); 

    assert(check_heap_validity());

    need_units = bytes_to_payload_units(size);

    /* First-fit scan: usable payload units = span - 2 (exclude header). */
    for (cur = s_free_head; cur != NULL; cur = chunk_get_next_free(cur)) {
        size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2;

        if (cur_payload >= need_units) {                     // accept exact fit too
            if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
                cur = split_for_alloc(cur, need_units);      // split only if remainder >= 2 units (H+F)
            } else {
                freelist_detach(cur);                        // exact fit
            }
            assert(check_heap_validity());
            return (void *)((char *)cur + CHUNK_UNIT);
        }
    }

    //none of free blocks fit the needed size:
    /* Need to grow the heap. */
    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    //Final split/detach on the grown block
    if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
        cur = split_for_alloc(cur, need_units);
    } else {
        freelist_detach(cur);
    }
    assert(check_heap_validity());
    return (void *)((char *)cur + CHUNK_UNIT); //return the payload pointer (header + one unit).
}


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
