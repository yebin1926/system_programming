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
// struct ChunkHeader {
//     int     status;
//     int     span;
//     Chunk_T next;
// };

// struct ChunkFooter {
//     int span;
//     Chunk_T prev;
// };

/* ----------------------- Getters / Setters ------------------------ */
int   chunk_get_status(Chunk_T c)                 { 
    assert(c != NULL);
    return c->status; 
}
void  chunk_set_status(Chunk_T c, int status)     { 
    assert(c != NULL);
    c->status = status; 
}

int   chunk_get_span_units(Chunk_T c)             { 
    assert(c != NULL);
    assert(c->span >= 2); /* header + footer at minimum */
    return c->span; 
}
void  chunk_set_span_units(Chunk_T c, int span_u) { 
    assert(c != NULL);
    assert(span_u >= 2);
    c->span = span_u; 
}

Chunk_T chunk_get_next_free(Chunk_T c)            { 
    assert(c != NULL);
    /* next may be NULL at the tail; allow that. */
    if (c->next == NULL) return NULL;
    assert(c->next != NULL);
    assert(c->next->status == CHUNK_FREE);
    return c->next; 
} //points to next FREE block's header

Chunk_T chunk_get_prev_free(Chunk_T c)            { 
    assert(c != NULL);
    assert(c->span >= 2);
    Chunk_FT f = get_footer_from_header(c);
    assert(f != NULL);
    /* prev may be NULL at the head; allow that. */
    if (f->prev == NULL) return NULL;
    assert(f->prev != NULL);
    assert(f->prev->status == CHUNK_FREE);
    return f->prev; 
} //points to prev FREE block's header

void    chunk_set_next_free(Chunk_T c, Chunk_T n) { 
    assert(c != NULL);
    /* Allow clearing the link. */
    if (n == NULL) { c->next = NULL; return; }
    assert(n != NULL);
    assert(n->status == CHUNK_FREE);
    c->next = n; 
}

void    chunk_set_prev_free(Chunk_T c, Chunk_T p) { 
    assert(c != NULL);
    assert(c->span >= 2);
    Chunk_FT f = get_footer_from_header(c);
    assert(f != NULL);
    /* Allow clearing the link. */
    if (p == NULL) { f->prev = NULL; return; }
    /* The assert below is about the *existing* prev link; add a guard so it doesn't deref NULL */
    if (f->prev != NULL) { assert(f->prev->status == CHUNK_FREE); }
    assert(p->status == CHUNK_FREE);
    f->prev = p; 
}

Chunk_T get_header_from_footer(Chunk_FT f) {
    assert(f != NULL);
    assert(f->span >= 2);
    Chunk_T h = (Chunk_T)((char*)f - (size_t)(f->span - 1) * (size_t)CHUNK_UNIT);
    /* h may still be NULL if this footer is before heap start; the caller must bounds-check. */
    if(h == NULL) return NULL;
    return h;
}

Chunk_FT get_footer_from_header(Chunk_T h) {
    assert(h != NULL);
    assert(h->span >= 2);
    Chunk_FT f = (Chunk_FT)((char*)h + (size_t)(h->span - 1) * (size_t)CHUNK_UNIT);
    /* f may still be out-of-bounds; the caller must bounds-check with heap end. */
    if(f == NULL) return NULL;
    return f;
}

/* chunk_get_adjacent:
 * Compute the next physical block header by jumping 'span' units
 * (1 header + payload + footer) forward from 'c'. If that address falls at or
 * beyond 'end', return NULL to indicate there is no next block. */
Chunk_T chunk_get_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T n;

    assert(c != NULL);
    assert(start != NULL);
    assert(end   != NULL);
    assert((void *)c >= start);
    assert(c->span >= 2);

    n = c + c->span;                /* span includes the header and footer */

    if ((void *)n >= end)
        return NULL;

    return n;
}

Chunk_T chunk_get_prev_adjacent(Chunk_T c, void *start, void *end)
{
    Chunk_T p;

    assert(c != NULL);
    assert(start != NULL);
    assert(end   != NULL);
    assert((void *)c < end);

    /* If c is at the very start of the heap, there is no previous block. */
    if ((void*)c <= start) return NULL;

    Chunk_FT prev_footer = (Chunk_FT)((char*)c - (size_t)CHUNK_UNIT);
    assert(prev_footer != NULL);
    assert(prev_footer->span >= 2);
    if ((void*)prev_footer < start) return NULL;
    p = get_header_from_footer(prev_footer);              /* span includes the header itself */

    if ((void *)p < start)
        return NULL;

    return p;
}

#ifndef NDEBUG


int chunk_is_valid(Chunk_T c, void *start, void *end)
{
    assert(c     != NULL);
    assert(start != NULL);
    assert(end   != NULL);

    /* Span must be at least header + footer, and header must be within bounds. */
    if (c->span < 2) { fprintf(stderr, "Span is less than 2\n"); return 0; }
    if (c < (Chunk_T)start) { fprintf(stderr, "Bad heap start (header)\n"); return 0; }
    if (c >= (Chunk_T)end)  { fprintf(stderr, "Bad heap end (header)\n");   return 0; }

    Chunk_FT footer = get_footer_from_header(c);
    if(footer == NULL) { fprintf(stderr, "Could not find footer\n"); return 0; }

    /* Define local aliases so the original checks below compile and refer to the same objects. */
    Chunk_FT f = footer;
    Chunk_T  C = c;

    /* Footer must also be inside bounds. */
    if (f < (Chunk_FT)start) { fprintf(stderr, "Bad heap start (footer)\n"); return 0; }
    if (f >= (Chunk_FT)end)  { fprintf(stderr, "Bad heap end (footer)\n");   return 0; }

    /* Non-positive spans are invalid. */
    if (c->span <= 0 || f->span <= 0) { fprintf(stderr, "Non-positive span\n"); return 0; }

    /* Minimum span must be >= 2 units (header + footer). */
    if (c->span < 2 || f->span < 2) { fprintf(stderr, "Span is less than 2\n"); return 0; }

    /* Header/footer span must agree. */
    if (C->span != footer->span) { fprintf(stderr, "Header and Footer span doesn't align\n"); return 0; }

    /* Optional: sanity that next-adjacent computed from header doesn't step past heap end. */
    {
        Chunk_T next_hdr = c + c->span;
        if ((void*)next_hdr > end) {
            fprintf(stderr, "Adjacency walks past heap end\n");
            return 0;
        }
    }

    return 1;
}
#endif