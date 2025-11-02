/*--------------------------------------------------------------------*/
/* test_heapmgr1_again.c                                              */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Bring in your implementation directly so we can test static helpers. */
#include "chunk.h"
#include "chunk.c"
#include "heapmgr1.c"

/* Tiny helper to print a banner */
static void banner(const char *name) {
    printf("\n==== %s ====\n", name);
}

/* Verify payload pointer maps to a valid USED header with expected size (>= req) */
static void check_alloc_block(void *p, size_t req_bytes) {
    assert(p != NULL);
    /* alignment: payload begins one CHUNK_UNIT after header */

    Chunk_T h = (Chunk_T)((char*)p - CHUNK_UNIT);
    assert(chunk_is_valid(h, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(h) == CHUNK_USED);

    /* span = 2 + payload_units; payload capacity in bytes: (span-2)*CHUNK_UNIT */
    int span_units = chunk_get_span_units(h);
    assert(span_units >= 2);
    size_t payload_bytes = (size_t)(span_units - 2) * CHUNK_UNIT;
    assert(payload_bytes >= req_bytes);

    /* footer agrees */
    Chunk_FT f = get_footer_from_header(h);
    assert(f != NULL);
    assert(f->span == span_units);
}

/* Verify a FREE block header has symmetric DLL links and correct footer */
static void check_free_block(Chunk_T h) {
    assert(h != NULL);
    assert(chunk_is_valid(h, s_heap_lo, s_heap_hi));
    assert(chunk_get_status(h) == CHUNK_FREE);

    /* Footer mirrors span */
    Chunk_FT f = get_footer_from_header(h);
    assert(f != NULL);
    assert(f->span == chunk_get_span_units(h));

    /* If head, prev must be NULL */
    if (h == s_free_head) {
        assert(chunk_get_prev_free(h) == NULL);
    }

    /* DLL symmetry (if neighbors exist) */
    Chunk_T nx = chunk_get_next_free(h);
    if (nx) {
        assert(chunk_get_status(nx) == CHUNK_FREE);
        assert(chunk_get_prev_free(nx) == h);
    }
    Chunk_T pv = chunk_get_prev_free(h);
    if (pv) {
        assert(chunk_get_status(pv) == CHUNK_FREE);
        assert(chunk_get_next_free(pv) == h);
    }
}

/* Exercise bytes_to_payload_units round-up */
static void test_bytes_to_units(void) {
    banner("bytes_to_payload_units()");
    assert(bytes_to_payload_units(0) == 0);
    assert(bytes_to_payload_units(1) == 1);
    assert(bytes_to_payload_units(CHUNK_UNIT - 1) == 1);
    assert(bytes_to_payload_units(CHUNK_UNIT) == 1);
    assert(bytes_to_payload_units(CHUNK_UNIT + 1) == 2);
    puts("  ok");
}

/* Bootstrap and initial grow */
static void test_bootstrap_and_grow(void) {
  printf("00000000000");
  banner("heap_bootstrap() + sys_grow_and_link()");
  printf("dskfnjgf");
  /* Lazy boot happens in malloc, but we can call bootstrap directly too */
  heap_bootstrap();
  assert(s_heap_lo != NULL && s_heap_hi != NULL);
  assert(s_heap_lo == s_heap_hi); /* empty heap initially */

  /* Grow explicitly and validate */
  Chunk_T c = sys_grow_and_link(8); /* request a small number of payload units */
  assert(c != NULL);
  assert(check_heap_validity());

  /* New/merged head must be FREE and valid */
  assert(s_free_head != NULL);
  check_free_block(s_free_head);
  puts("  ok");
}

/* Split a free block and confirm remainder stays free, tail is used */
static void test_split_for_alloc(void) {
    banner("split_for_alloc()");
    /* Ensure there is a reasonably large free block at head */
    Chunk_T head = s_free_head;
    if (!head || (chunk_get_span_units(head) - 2) < 8) {
        (void)sys_grow_and_link(16);
        head = s_free_head;
    }
    assert(head != NULL);
    check_free_block(head);

    size_t need_units = 3; /* 3 payload units */
    int old_span = chunk_get_span_units(head);

    Chunk_T alloc = split_for_alloc(head, need_units);
    assert(alloc != NULL);
    assert(check_heap_validity());

    /* Alloc block must be USED and not in DLL */
    assert(chunk_get_status(alloc) == CHUNK_USED);
    assert(chunk_get_next_free(alloc) == NULL);
    assert(chunk_get_prev_free(alloc) == NULL);

    /* Remainder head still free, spans add up */
    Chunk_T rem = s_free_head; /* head may still be the same pointer (leading piece) */
    check_free_block(rem);
    int alloc_span = chunk_get_span_units(alloc);
    int rem_span   = chunk_get_span_units(rem);
    assert(alloc_span == (int)(2 + need_units));
    assert(rem_span + alloc_span == old_span);

    /* And they are physically adjacent: rem → alloc */
    assert(chunk_get_adjacent(rem, s_heap_lo, s_heap_hi) == alloc);

    /* Free the allocated block to restore */
    heapmgr_free((char*)alloc + CHUNK_UNIT);
    assert(check_heap_validity());
    puts("  ok");
}

/* Public API: malloc/free cycles with various sizes and coalescing */
static void test_malloc_free_sequence(void) {
    banner("heapmgr_malloc() / heapmgr_free()");
    /* Ensure head is reasonably big */
    (void)sys_grow_and_link(64);

    void *p1 = heapmgr_malloc(24); /* 2 units (assuming CHUNK_UNIT=16) */
    void *p2 = heapmgr_malloc(48); /* 3 units                       */
    void *p3 = heapmgr_malloc(1);  /* 1 unit                        */

    check_alloc_block(p1, 24);
    check_alloc_block(p2, 48);
    check_alloc_block(p3, 1);

    /* Free in different order, verify coalescing via validator and free-head checks */
    heapmgr_free(p2);
    assert(check_heap_validity());
    heapmgr_free(p1);
    assert(check_heap_validity());
    heapmgr_free(p3);
    assert(check_heap_validity());

    /* After freeing all, head must be FREE and large (coalesced region). */
    assert(s_free_head != NULL);
    check_free_block(s_free_head);
    puts("  ok");
}

/* Explicit coalescing scenario via free order */
static void test_free_coalesce_neighbors(void) {
    banner("coalescing via freelist_push_front()");
    /* Grow a bit to ensure contiguous space */
    (void)sys_grow_and_link(64);

    /* Allocate three adjacent small blocks (likely carved from the same region) */
    void *a = heapmgr_malloc(CHUNK_UNIT);       /* 1 unit */
    void *b = heapmgr_malloc(CHUNK_UNIT);       /* 1 unit */
    void *c = heapmgr_malloc(2*CHUNK_UNIT);     /* 2 units */

    Chunk_T ha = (Chunk_T)((char*)a - CHUNK_UNIT);
    Chunk_T hb = (Chunk_T)((char*)b - CHUNK_UNIT);
    Chunk_T hc = (Chunk_T)((char*)c - CHUNK_UNIT);

    /* Physically: ha → hb → hc (very likely, given split-from-head policy) */
    assert(chunk_get_adjacent(ha, s_heap_lo, s_heap_hi) == hb);
    assert(chunk_get_adjacent(hb, s_heap_lo, s_heap_hi) == hc);

    /* Free middle first, then neighbors, ensure coalescing occurs */
    heapmgr_free(b);
    assert(check_heap_validity());
    heapmgr_free(a);
    assert(check_heap_validity());
    heapmgr_free(c);
    assert(check_heap_validity());

    /* Now the three should coalesce into one bigger FREE block somewhere in list */
    /* Walk physical memory and ensure no adjacent FREE blocks remain */
    for (Chunk_T w = (Chunk_T)s_heap_lo; w && w < (Chunk_T)s_heap_hi; w = chunk_get_adjacent(w, s_heap_lo, s_heap_hi)) {
        if (chunk_get_status(w) == CHUNK_FREE) {
            Chunk_T n = chunk_get_adjacent(w, s_heap_lo, s_heap_hi);
            if (n) assert(chunk_get_status(n) != CHUNK_FREE);
        }
    }
    puts("  ok");
}

int main(void)
{
    printf("Running heapmgr1 (DLL + boundary tags) tests...\n");

    test_bytes_to_units();
    test_bootstrap_and_grow();
    test_split_for_alloc();
    test_malloc_free_sequence();
    test_free_coalesce_neighbors();

    printf("\nAll tests passed.\n");
    return 0;
}