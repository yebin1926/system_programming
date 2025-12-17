/*--------------------------------------------------------------------*/
/* rwlock.c                                                           */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/* Modified by: Yebin Pyun                                        */
/*--------------------------------------------------------------------*/
#include "rwlock.h"
/*--------------------------------------------------------------------*/
typedef enum { REQ_READ = 1, REQ_WRITE = 2 } req_type_t;

struct uctx //hook that lets you add internal data structures your RW lock needs, while keeping official rwlock_t definition unchanged
{
    /* free to use */
    pthread_cond_t cv;
    unsigned int next_element;
    unsigned int oldest_element;

    unsigned int qr_waiters; //how many quick readers are currently waiting because a writer is active
    int read_batch_end; //the end of the currently granted “reader batch”.

    req_type_t request_type[WRITER_RING_SIZE]; //array mapping each element to its request type
    int slot_table[WRITER_RING_SIZE]; //stores the exact ticket number currently occupying each ring slot
};
/*--------------------------------------------------------------------*/
int rwlock_init(rwlock_t *rw, int delay)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    if(rw == NULL){
        errno = EINVAL;
        return -1;
    }
    rw->current_readers = 0;
    rw->current_writers = 0;
    rw->delay = delay;

    if (pthread_mutex_init(&rw->lock, NULL) != 0) {
        errno = rc;
        return -1;
    }

    struct uctx *uctx = malloc(sizeof(*uctx));
    if(uctx == NULL) {
        errno = ENOMEM;
        pthread_mutex_destroy(&rw->lock);
        return -1;
    }

    if (pthread_cond_init(&uctx->cv, NULL) != 0){
        errno = EINVAL;
        free(uctx);
        pthread_mutex_destroy(&rw->lock);
        return -1;
    }

    uctx->next_element = 0;
    uctx->oldest_element = 0;
    uctx->qr_waiters = 0;
    uctx->read_batch_end = -1;

    for(int i=0; i<WRITER_RING_SIZE; i++){
        uctx->request_type[i] = 0;
        uctx->slot_table[i] = -1;
    }

    rw->uctx = uctx;

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_read_lock(rwlock_t *rw, int quick)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    if(rw == NULL){
        errno = EINTR;
        return -1;
    }
    rwlock_read_lock(rw);
    if(quick){ //QREAD mode
        if(rw->current_readers == 0) rw->current_readers++; //there are no readers
        else if(rw->current_readers > 0){ //there are readers
            rw->uctx->qr_waiters++;
        }
        while(rw->current_writers > 0){ //there is a writer
            pthread_cond_wait(rw->uctx->qr_cond);
        }
        uctx->quick_read_waiters--;
        rw->current_readers++:

        rwlock_read_unlock(rw);
        return 0; 
    } else { //Normal read mode
        int my_turn = rw->uctx->next_element;
        rw->uctx->next_element++;
        rw->uctx->pending_table[next_element % WRITER_RING_SIZE] = 0;
        if(rw->current_writers == 0 && my_turn <= uctx->reader_batch_end_ticket){
            rw->current_readers++;
            rw->uctx->pending_table[my_turn] = 0;

        }
    }

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_read_unlock(rwlock_t *rw)
{
    TRACE_PRINT();
    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    sleep(rw->delay);
/*--------------------------------------------------------------------*/
    /* edit here */

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_write_lock(rwlock_t *rw)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_write_unlock(rwlock_t *rw)
{
    TRACE_PRINT();
    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    sleep(rw->delay);
/*--------------------------------------------------------------------*/
    /* edit here */

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_destroy(rwlock_t *rw)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */

/*--------------------------------------------------------------------*/

    return 0;
}