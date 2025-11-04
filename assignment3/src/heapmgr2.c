/*--------------------------------------------------------------------*/
/* heapmgr2.c                                                      */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#include "chunk.h"

#define FALSE 0
#define TRUE  1
#define NUM_BINS 10  

#define TINY_BYTES        1000
#define TINY_SPAN         ((int)(2 + bytes_to_payload_units(TINY_BYTES)))

enum { SYS_MIN_ALLOC_UNITS = 1024 };

static Chunk_T s_bins[NUM_BINS] = { NULL };

/* Heap bounds: [s_heap_lo, s_heap_hi). */
void *s_heap_lo = NULL, *s_heap_hi = NULL;

int s_heap_booted = FALSE;

#ifndef NDEBUG

#endif

static size_t bytes_to_payload_units(size_t bytes)
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

static int get_bin_index(int span_units){
    if(span_units < 8) return 0;
    else if(span_units < 16) return 1;
    else if(span_units < 32) return 2;
    else if(span_units < 64) return 3;
    else if(span_units < 128) return 4;
    else if(span_units < 256) return 5;
    else if(span_units < 512) return 6;
    else if(span_units < 1024) return 7;
    else if(span_units < 2048) return 8;
    else return 9;
}

#ifndef NDEBUG

static int check_heap_validity(void)
{
    Chunk_T w;
    
    //makes sure heap is initialized.
    if (s_heap_lo == NULL) { fprintf(stderr, "Uninitialized heap start\n"); return FALSE; }
    if (s_heap_hi == NULL) { fprintf(stderr, "Uninitialized heap end\n");   return FALSE; }

    if (s_heap_lo == s_heap_hi) {
        for(int i = 0; i < NUM_BINS; i++){
            if(s_bins[i] != NULL){
                fprintf(stderr, "Inconsistent empty heap: bin %d not empty\n", i);
                return FALSE;
            }
        }
        return TRUE; //if heap is empty, the bin list should also be empty
    }

    /* Walk all physical blocks in address order. and check if their chunk is valid*/
    for (w = (Chunk_T)s_heap_lo; w && w < (Chunk_T)s_heap_hi;
         w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
    }

    /* Walk the bin list; ensure nodes are free and not trivially coalescible. */
    for(int i = 0; i < NUM_BINS; i++){
        Chunk_T head = s_bins[i];
        if (head && chunk_get_prev_free(head) != NULL) {
            fprintf(stderr, "Bin %d head has non-NULL prev\n", i);
            return FALSE;
        }

        for (w = head; w; w = chunk_get_next_free(w)) {
            Chunk_T n;

            if(w == head && chunk_get_prev_free(w)) return FALSE;

            if (chunk_get_status(w) != CHUNK_FREE) { //check if there are any non-free chunks in the free list
                fprintf(stderr, "Non-free chunk in the free list\n");
                return FALSE;
            }
            if (!chunk_is_valid(w, s_heap_lo, s_heap_hi)) return FALSE;
            
            if (get_bin_index(chunk_get_span_units(w)) != i) {
                fprintf(stderr, "Wrong bin for span in bin %d\n", i);
                return FALSE;
            }

            n = chunk_get_adjacent(w, s_heap_lo, s_heap_hi); // checks if any chunk was forgotten to be coalesced
            if (n && chunk_get_status(n) == CHUNK_FREE) {
                fprintf(stderr, "Uncoalesced adjacent free chunks\n");
                return FALSE;
            }

            //checking if next's prev_free is w - symmetry
            n = chunk_get_next_free(w);
            if( n && chunk_get_prev_free(n) != w) {
                fprintf(stderr, "Next->prev symmetry broken in bin %d\n", i);
                return FALSE;
            }

            //checking if prev's next is w - symmetry
            n = chunk_get_prev_free(w);
            if (n == NULL) {
                // if no prev, this must be the head of this bin
                if (w != head) {
                    fprintf(stderr, "Non-head node with NULL prev in bin %d\n", i);
                    return FALSE;
                }
            } else {
                if (chunk_get_next_free(n) != w) {
                    fprintf(stderr, "Prev->next symmetry broken in bin %d\n", i);
                    return FALSE;
                }
            }
        }
    }
    
    return TRUE;
}
#endif /* NDEBUG */

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

static void bin_detach(Chunk_T c)
{
    Chunk_T prev = chunk_get_prev_free(c);
    Chunk_T next = chunk_get_next_free(c);

    assert(c != NULL);
    assert(chunk_get_status(c) == CHUNK_FREE);

    int bin_index = get_bin_index(chunk_get_span_units(c));

    if (prev == NULL){
        assert(s_bins[bin_index] == c);
        s_bins[bin_index] = next;
        if (s_bins[bin_index] != NULL) {
            chunk_set_prev_free(s_bins[bin_index], NULL);
        }
        if (next) chunk_set_prev_free(next, NULL);
    }
    else{
        chunk_set_next_free(prev, next);
        if (next) chunk_set_prev_free(next, prev);
    }

    if (prev != NULL) assert(chunk_get_next_free(prev) == next);
    if (next != NULL) assert(chunk_get_prev_free(next) == prev);
    if (s_bins[bin_index] != NULL) assert(chunk_get_prev_free(s_bins[bin_index]) == NULL);

    chunk_set_next_free(c, NULL);
    chunk_set_prev_free(c, NULL);
    // chunk_set_status(c, CHUNK_USED);
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
    // Chunk_T an = chunk_get_next_free(a);
    // Chunk_T bn = chunk_get_next_free(b);

    // /* Remove b from free list first (no status change). */
    // bin_detach(b);

    /* Grow 'a' */
    chunk_set_span_units(a, chunk_get_span_units(a) + chunk_get_span_units(b));

    /* Fix 'a'->next (if 'an' was b, switch to 'bn') */
    // if (an == b) an = bn;
    // chunk_set_next_free(a, an);
    // if (an) chunk_set_prev_free(an, a);
    chunk_set_prev_free(a, NULL);
    chunk_set_next_free(a, NULL);

    return a;
}

static void bin_push_front(Chunk_T c)
{
    assert(c && s_heap_lo && s_heap_hi);
    assert((void*)c >= s_heap_lo && (void*)c < s_heap_hi);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_span_units(c) >= 2);

    /* 1) Mark FREE and insert at head */
    chunk_set_status(c, CHUNK_FREE);
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, NULL);

    // 2) Coalesce with physical NEXT first (survivor stays 'c')
    {
        Chunk_T next = chunk_get_adjacent(c, s_heap_lo, s_heap_hi);
        if (next && chunk_get_status(next) == CHUNK_FREE) {
            bin_detach(next); 
            c = coalesce_two(c, next); /* survivor is 'c' */
            /* after coalesce_two, if c is head, its prev is NULL already */
        }
    }
    { // 3) Coalesce with physical PREV
        Chunk_T prev = chunk_get_prev_adjacent(c, s_heap_lo, s_heap_hi);
        if (prev && chunk_get_status(prev) == CHUNK_FREE) {
            //Chunk_T old_next_head = chunk_get_next_free(c);
            bin_detach(prev); 
            c = coalesce_two(prev, c);
        }
    }

    // place on front of head
    int bin_index = get_bin_index(chunk_get_span_units(c));
    chunk_set_prev_free(c, NULL);
    chunk_set_next_free(c, s_bins[bin_index]);
    if (s_bins[bin_index]) chunk_set_prev_free(s_bins[bin_index], c);
    s_bins[bin_index] = c;

    assert(s_bins[bin_index] != NULL);
    assert(chunk_get_prev_free(s_bins[bin_index]) == NULL);
    assert(chunk_is_valid(c, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(c) == CHUNK_FREE);
}

static Chunk_T sys_grow_and_link(size_t need_units)
{
    Chunk_T c;
    size_t grow_data = (need_units < SYS_MIN_ALLOC_UNITS) ? SYS_MIN_ALLOC_UNITS : need_units;
    size_t grow_span = 2 + grow_data;

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
    bin_push_front(c);
    assert(check_heap_validity());

    return c;
}


void * heapmgr_malloc(size_t size)
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
    int bin_index = get_bin_index(2 + (int)need_units);
    for(int i = bin_index; i < NUM_BINS; i++){
        for (cur = s_bins[i]; cur != NULL; cur = chunk_get_next_free(cur)) {
            size_t cur_payload = (size_t)chunk_get_span_units(cur) - 2;

            if (cur_payload >= need_units) {                     // accept exact fit too
                bin_detach(cur);
                if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
                    Chunk_T alloc = split_for_alloc(cur, need_units);      // split only if remainder >= 2 units (H+F)
                    chunk_set_status(alloc, CHUNK_USED);
                    bin_push_front(cur);
                    return (char*)alloc + CHUNK_UNIT;
                } else {    // exact fit
                    chunk_set_status(cur, CHUNK_USED);
                    return (char*)cur + CHUNK_UNIT;                     
                }
            }
        }
    }
    

    //none of free blocks fit the needed size:
    /* Need to grow the heap. */
    cur = sys_grow_and_link(need_units);
    if (cur == NULL) {
        assert(check_heap_validity());
        return NULL;
    }

    bin_detach(cur);
    //Final split/detach on the grown block
    if ((size_t)chunk_get_span_units(cur) >= (size_t)(2 + need_units + 2)) {
        Chunk_T alloc = split_for_alloc(cur, need_units);
        chunk_set_status(alloc, CHUNK_USED);
        bin_push_front(cur);                                 // re-bin remainder
        return (char*)alloc + CHUNK_UNIT;
    } else {
        chunk_set_status(cur, CHUNK_USED);
        return (char*)cur + CHUNK_UNIT;
    }
    // return (void *)((char *)cur + CHUNK_UNIT); //return the payload pointer (header + one unit).
}


void heapmgr_free(void *p)
{
    Chunk_T c;
    //Do the chunk_set_status(..., CHUNK_USED) in your malloc/allocation path ?

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

    bin_push_front(c);

    assert(chunk_get_status(c) == CHUNK_FREE);                           // block should now be marked FREE
    assert(check_heap_validity());
}