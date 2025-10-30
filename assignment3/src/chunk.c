/*--------------------------------------------------------------------*/
/* chunk.c                                                        */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "chunk.h"

/* Internal header layout
 * - status: CHUNK_FREE or CHUNK_USED
 * - span:   total units, including the header and footer. Span = # of chunk units that one complete memory block occupies
 * - next:   next-free pointer for the singly-linked free list
 */
struct ChunkHeader {
    int     status;
    int     span;
    Chunk_T next;
};

struct ChunkFooter {
    int span;
    Chunk_T prev;
};

/* ----------------------- Getters / Setters ------------------------ */
int   chunk_get_status(Chunk_T c)                 { return c->status; }
void  chunk_set_status(Chunk_T c, int status)     { c->status = status; }

int   chunk_get_span_units(Chunk_T c)             { return c->span; }
void  chunk_set_span_units(Chunk_T c, int span_u) { c->span = span_u; }

Chunk_T chunk_get_next_free(Chunk_T c)            { return c->next; } //points to next FREE block's header
Chunk_T chunk_get_prev_free(Chunk_T c)            { return c->prev; } //points to prev FREE block's header
void    chunk_set_next_free(Chunk_T c, Chunk_T n) { c->next = n; }
void    chunk_set_prev_free(Chunk_T c, Chunk_T p) { c->prev = p; }

Chunk_T get_header_from_footer(Chunk_FT f) {
    Chunk_T h = f - (f->span - 1);
    if(h == NULL) return NULL;
    return h;
}

Chunk_FT get_footer_from_header(Chunk_T h) {
    Chunk_FT f = h + (h->span - 1);
    if(f == NULL) return NULL;
    return f;
}

/* chunk_get_adjacent:
 * Compute the next physical block header by jumping 'span' units
 * (1 header + payload) forward from 'c'. If that address falls at or
 * beyond 'end', return NULL to indicate there is no next block. */
Chunk_T chunk_get_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T n;

    assert((void *)c >= start);

    n = c + c->span;                /* span includes the header and footer */

    if ((void *)n >= end)
        return NULL;

    return n;
}

Chunk_T chunk_get_prev_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T p;

    assert((void *)c < end);

    Chunk_FT prev_footer = c - 1;
    if (prev_footer == NULL) return NULL;
    p = get_header_from_footer(prev_footer);              /* span includes the header itself */

    if ((void *)p < start)
        return NULL;

    return p;
}

#ifndef NDEBUG
/* chunk_is_valid:
 * Minimal per-block validity checks used by the heap validator:
 *  - c must lie within [start, end)
 *  - span must be positive (non-zero) */
int chunk_is_valid(Chunk_T c, void *start, void *end)
{
    assert(c     != NULL);
    assert(start != NULL);
    assert(end   != NULL);

    Chunk_FT footer = c + (span - 2);
    if(footer == NULL) { fprintf(stderr, "Could not find footer\n"); return 0; }
    
    //checks if chunk is within the bounds of heap, and has a positive span
    if (c < (Chunk_T)start) { fprintf(stderr, "Bad heap start\n"); return 0; }
    if (c >= (Chunk_T)end)  { fprintf(stderr, "Bad heap end\n");   return 0; }
    if (c->span <= 0)       { fprintf(stderr, "Non-positive span\n"); return 0; }
    if (C->span != footer->span) {fprintf(stderr, "Header and Footer span doesn't align\n"); return 0;}
    return 1;
}
#endif
