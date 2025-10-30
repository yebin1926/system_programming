/*--------------------------------------------------------------------*/
/* chunk.h                                                        */
/*--------------------------------------------------------------------*/

// which checking or assert statements should i add?

#ifndef _CHUNK_H_
#define _CHUNK_H_

#pragma once

#include <stdbool.h>
#include <unistd.h>

struct ChunkHeader;
struct ChunkFooter;

typedef struct ChunkHeader *Chunk_T;
typedef struct ChunkFooter *Chunk_FT;


struct ChunkHeader {
    int     status;  /* CHUNK_FREE or CHUNK_USED */
    int     span;    /* in UNITS; includes header + footer */
    Chunk_T next;    /* next free block (header pointer) */
};

struct ChunkFooter {
    int     span;    /* mirror span (in UNITS) */
    Chunk_T prev;    /* prev free block (header pointer) */
};

/* Status flags */
enum {
    CHUNK_FREE = 0,
    CHUNK_USED = 1,
};

/* Chunk unit size (bytes). This equals sizeof(struct Chunk) in this baseline. */
enum {
    CHUNK_UNIT = 16,
};

/* ----------------------- Getters / Setters ------------------------ */

/* chunk_get_status / chunk_set_status:
 * Get/Set the status of a block (CHUNK_FREE or CHUNK_USED). */
int   chunk_get_status(Chunk_T c);
void  chunk_set_status(Chunk_T c, int status);

/* chunk_get_span_units / chunk_set_span_units:
 * Get/Set the total number of units of the block, including its header.*/
int   chunk_get_span_units(Chunk_T c);
void  chunk_set_span_units(Chunk_T c, int span_units);

/* chunk_get_next_free / chunk_set_next_free:
 * Get/Set the next pointer in the singly-linked free list. */
Chunk_T chunk_get_next_free(Chunk_T c);
Chunk_T chunk_get_prev_free(Chunk_T c);
void    chunk_set_next_free(Chunk_T c, Chunk_T next);
void    chunk_set_prev_free(Chunk_T c, Chunk_T prev);

Chunk_T get_header_from_footer(Chunk_FT f);
Chunk_FT get_footer_from_header(Chunk_T h);

Chunk_T chunk_get_adjacent(Chunk_T c, void *start, void *end);
Chunk_T chunk_get_prev_adjacent(Chunk_T c, void *start, void *end);

/* Debug-only sanity check (compiled only if NDEBUG is not defined). */
#ifndef NDEBUG

/* chunk_is_valid:
 * Return 1 iff 'c' lies within [start, end) and has a positive span. */
int   chunk_is_valid(Chunk_T c, void *start, void *end);

#endif

#endif /* _CHUNK_H_ */
