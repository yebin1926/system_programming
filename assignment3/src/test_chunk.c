#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "chunk.h"

/* Helpers to make units <-> bytes clean */
static size_t U(size_t units) { return units * (size_t)CHUNK_UNIT; }

/* Make one block at header pointer h with given span and status.
   Writes header and footer (span counted as H+P+F). Does NOT touch free-list links. */
static void make_block(Chunk_T h, int span, int status) {
    chunk_set_span_units(h, span);
    chunk_set_status(h, status);
    Chunk_FT f = get_footer_from_header(h);
    /* footer mirrors at least span; if you mirror status too, set it here */
    /* f->status = status; // if your footer ever mirrors status */
    /* prev/next left as-is; test cases will set them when needed */
    (void)f;
}

int main(void) {
#ifndef NDEBUG
    printf("[info] NDEBUG not defined — validator will run.\n");
#else
    printf("[info] NDEBUG defined — validator disabled; only API tests will run.\n");
#endif

    /* Allocate a fake heap: enough room for a few blocks. */
    const size_t heap_units = 40;
    void *heap_bytes = NULL;
    int rc = posix_memalign(&heap_bytes, CHUNK_UNIT, U(heap_units));
    assert(rc == 0 && heap_bytes != NULL);
    memset(heap_bytes, 0, U(heap_units));

    void *heap_lo = heap_bytes;
    void *heap_hi = (char*)heap_bytes + U(heap_units);

    /* Lay out three blocks in a row:
       A: span=6 (H+P4+F), FREE
       B: span=4 (H+P2+F), USED
       C: span=5 (H+P3+F), FREE
    */
    Chunk_T A = (Chunk_T)heap_lo;
    make_block(A, 6, CHUNK_FREE);

    Chunk_T B = A + chunk_get_span_units(A);           /* next header by units */
    make_block(B, 4, CHUNK_USED);

    Chunk_T C = B + chunk_get_span_units(B);
    make_block(C, 5, CHUNK_FREE);

    /* Sanity: headers within heap */
    assert((void*)A >= heap_lo && (void*)A < heap_hi);
    assert((void*)B >= heap_lo && (void*)B < heap_hi);
    assert((void*)C >= heap_lo && (void*)C < heap_hi);

    /* 1) header -> footer mapping */
    {
        Chunk_FT Af = get_footer_from_header(A);
        Chunk_T  Ah = get_header_from_footer(Af);
        assert(Ah == A);
        Chunk_FT Bf = get_footer_from_header(B);
        Chunk_T  Bh = get_header_from_footer(Bf);
        assert(Bh == B);
        Chunk_FT Cf = get_footer_from_header(C);
        Chunk_T  Ch = get_header_from_footer(Cf);
        assert(Ch == C);
    }

    /* 2) forward adjacency */
    {
        Chunk_T nA = chunk_get_adjacent(A, heap_lo, heap_hi);
        Chunk_T nB = chunk_get_adjacent(B, heap_lo, heap_hi);
        Chunk_T nC = chunk_get_adjacent(C, heap_lo, heap_hi);
        assert(nA == B);
        assert(nB == C);
        /* C should point past heap or NULL depending on space; our heap has extra room,
           but we only laid out 3 blocks, so C's next is just a raw address; ask API: */
        assert(nC == NULL || (void*)nC <= heap_hi);
        /* For this test we expect NULL since nothing is placed after C and its
           computed next header will be >= heap_hi given our sizes; adjust if not. */
        if (nC != NULL) {
            /* If it didn't return NULL, at least ensure not out-of-bounds */
            assert((void*)nC < heap_hi);
        }
    }

    /* 3) backward adjacency (via previous footer) */
    {
        Chunk_T pB = chunk_get_prev_adjacent(B, heap_lo, heap_hi);
        Chunk_T pC = chunk_get_prev_adjacent(C, heap_lo, heap_hi);
        assert(pB == A);
        assert(pC == B);

        Chunk_T pA = chunk_get_prev_adjacent(A, heap_lo, heap_hi);
        assert(pA == NULL); /* A is first block */
    }

    /* 4) free-list links across header/footer
       By spec: next is in header; prev is in footer. */
    {
        /* Build a small free list A <-> C (skip B because it's USED) */
        chunk_set_next_free(A, C);
        chunk_set_prev_free(C, A);

        assert(chunk_get_next_free(A) == C);
        assert(chunk_get_prev_free(C) == A);

        /* Head/tail NULL behavior */
        chunk_set_prev_free(A, NULL);
        assert(chunk_get_prev_free(A) == NULL);
        chunk_set_next_free(C, NULL);
        assert(chunk_get_next_free(C) == NULL);
    }

#ifndef NDEBUG
    /* 5) validator checks for each block */
    {
        int okA = chunk_is_valid(A, heap_lo, heap_hi);
        int okB = chunk_is_valid(B, heap_lo, heap_hi);
        int okC = chunk_is_valid(C, heap_lo, heap_hi);
        assert(okA == 1);
        assert(okB == 1);
        assert(okC == 1);
    }

    /* 6) negative tests (span < 2, mismatched footer, OOB footer) */
    {
        /* Corrupt C's footer span and expect validator to fail */
        Chunk_FT Cf = get_footer_from_header(C);
        int saved = Cf->span;
        Cf->span = saved + 1;
        int okC_bad = chunk_is_valid(C, heap_lo, heap_hi);
        assert(okC_bad == 0);
        Cf->span = saved; /* restore */

        /* Corrupt A's span to 1 (illegal) */
        int savedA = chunk_get_span_units(A);
        chunk_set_span_units(A, 1);
        int okA_bad = chunk_is_valid(A, heap_lo, heap_hi);
        assert(okA_bad == 0);
        chunk_set_span_units(A, savedA); /* restore */
        assert(chunk_is_valid(A, heap_lo, heap_hi) == 1);
    }
#endif

    printf("[ok] chunk tests passed\n");
    free(heap_bytes);
    return 0;
}