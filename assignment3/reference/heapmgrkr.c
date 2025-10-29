/*--------------------------------------------------------------------*/
/* heapmrgkr.c                                                        */
/* Author: Bob Dondero, nearly identical to code from the K&R book    */
/*--------------------------------------------------------------------*/

#include "heapmgr.h"

struct header {       /* block header */
   struct header *ptr; /* next block if on free list */
   unsigned size;     /* size of this block, header + payload tgt */
};

typedef struct header Header;

static Header base;       /* dummy block to get started */
static Header *freep = NULL;     /* pointer that points to start of free list -- will be updated to remember last pos for next CALL */

static Header *morecore(unsigned);

/* malloc:  general-purpose storage allocator */
void *heapmgr_malloc(size_t nbytes)
{
    Header *p, *prevp;      // p: The current node while walking through the circular free list, for a SINGLE CALL
                            // prevp: The previous node in the free list — needed when you modify or remove a block 
    unsigned nunits;        //requested size rounded up to a multiple of sizeof(Header), plus one for the header.

    nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) + 1;
    if ((prevp = freep) == NULL) {                  //initialize chain to have one-node circle (base.ptr == &base) and set sizes to zero
        base.ptr = freep = prevp = &base;
        base.size = 0;
    }

    //Walk the chain with prevp and p (previous and current free block).
    for (p = prevp->ptr; ; prevp = p, p = p->ptr) { // 
        //First-fit
        if (p->size >= nunits) {        /* big enough */
            if (p->size == nunits)      /* if exact fit, unlink p from chain */
                prevp->ptr = p->ptr;
            else {                      /* if block is larger than requested, split from the tail */
                p->size -= nunits;      //shrink the free block
                p += p->size;           //move pointer forward by the remaining free block size - landing at the start of the tail.
                p->size = nunits;       //write nunits on that tail; that tail is the allocated block.
            }
            freep = prevp;
            return (void*)(p+1);        //it returns a pointer to just after the header -- for the actual payload
        }
        if (p == freep)  /* wrapped around free list */
            if ((p = morecore(nunits)) == NULL)
                return NULL;   /* none left */
    }
}

#define NALLOC 1024

/* morecore:  ask system for more memory */
static Header *morecore(unsigned int nu)
{
    char *cp, *sbrk(int);
    Header *up;

    if (nu < NALLOC)
        nu = NALLOC;
    cp = sbrk(nu * sizeof(Header));
    if (cp == (char *) -1)  /* no space at all */
        return NULL;
    up = (Header *) cp;
    up->size = nu;
    heapmgr_free((void *)(up+1));
    return freep;
}

/* free:  put block ap in free list */
void heapmgr_free(void *ap) //btw: the free list is sorted by increasing addr order
{
    Header *bp, *p;

    bp = (Header *)ap - 1;    /* since we worked with addr of payload, go back to block header */
    //now bp is the metadata for the block we're freeing
    for (p = freep; !(bp > p && bp < p->ptr); p = p->ptr) //end when bp belongs between p and p->ptr in address order
        if (p >= p->ptr && (bp > p || bp < p->ptr)) //if next block is first block AND bp should be the new last or first block
            break;  /* freed block at start or end of arena */ //stop bc we know where to insert bp now.

    //Coalescing Process
    if (bp + bp->size == p->ptr) {      //is the addr right after bp same as starting addr of next block?
        bp->size += p->ptr->size;       /* join to upper/next nbr */
        bp->ptr = p->ptr->ptr;
    } else
        bp->ptr = p->ptr;
    if (p + p->size == bp) {            //is the addr right after p same as bp's starting addr?  
        p->size += bp->size;            /* join to lower/prev nbr */
        p->ptr = bp->ptr;
    } else
        p->ptr = bp;
    freep = p;
}