/*--------------------------------------------------------------------*/
/* rwlock.c                                                           */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/* Modified by: Yebin Pyun                                        */
/*--------------------------------------------------------------------*/
#include "rwlock.h"
/*--------------------------------------------------------------------*/
typedef enum { REQ_NONE = 0, REQ_READ = 1, REQ_WRITE = 2 } req_type_t;

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
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&rw->lock);

    if(quick){ //QREAD mode
        if(rw->current_writers == 0) {//there are no existing writers
            rw->current_readers++; 
            pthread_mutex_unlock(&rw->lock);
            return 0;
        }
        //else, there are existing writers
        rw->uctx->qr_waiters++;

        while(rw->current_writers > 0){ //there is a writer
            pthread_cond_wait(&rw->uctx->cv, &rw->lock);
        }
        rw->uctx->qr_waiters--;
        rw->current_readers++;

        pthread_cond_broadcast(&rw->uctx->cv);
        pthread_mutex_unlock(&rw->lock);
        return 0; 

    } else { //Normal read mode
        int my_turn = rw->uctx->next_element;
        rw->uctx->next_element++;
        rw->uctx->slot_table[my_turn % WRITER_RING_SIZE] = my_turn;
        rw->uctx->request_type[my_turn % WRITER_RING_SIZE] = REQ_READ;

        while(1){
            //if active writer exists, wait
            if(rw->current_writers > 0){
                pthread_cond_wait(&rw->uctx->cv, &rw->lock);
                continue;
            }
            //if this reader is in reader batch, proceed
            if(my_turn <= rw->uctx->read_batch_end && rw->current_writers == 0){
                rw->uctx->slot_table[my_turn % WRITER_RING_SIZE] = -1;
                rw->uctx->request_type[my_turn % WRITER_RING_SIZE] = REQ_NONE;
                rw->current_readers += 1;
                pthread_mutex_unlock(&rw->lock);
                return 0; 
            }

            //if no batch is active, check if it's the head, and who should be in the batch
            int head = rw->uctx->oldest_element;
            if(my_turn == head && rw->uctx->request_type[head % WRITER_RING_SIZE] == REQ_READ){
                int idx = head % WRITER_RING_SIZE;
                while(rw->uctx->slot_table[idx] == head && rw->uctx->request_type[head % WRITER_RING_SIZE] == REQ_READ){ //find the last element in batch
                    head++;
                }
                head--;
                rw->uctx->read_batch_end = head;
                rw->uctx->oldest_element = head+1;

                pthread_cond_broadcast(&rw->uctx->cv);
                continue;
            }

            //otherwise, not allowed yet
            pthread_cond_wait(&rw->uctx->cv, &rw->lock);
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
    pthread_mutex_lock(&rw->lock);
    rw->current_readers--;
    if(rw->current_readers > 0){ //other readers still hold the block
        pthread_mutex_unlock(&rw->lock);
        return 0;
    }
    if(rw->current_readers == 0){ //if there are no readers now, signal oldest element in fifo
        // if(rw->uctx->qr_waiters > 0){ //if there are pending QR readers
        //     pthread_cond_broadcast(&rw->uctx->cv);
        //     return 0;
        // }
        pthread_cond_signal(&rw->uctx->cv);
        // int next_oldest = rw->uctx->oldest_element++;
        // if(rw->uctx->request_type[next_oldest % WRITER_RING_SIZE] == REQ_READ){
        //     rwlock_read_lock(rw, 0);
        // } else if(rw->uctx->request_type[next_oldest % WRITER_RING_SIZE] == REQ_WRITE){
        //     rwlock_write_lock(rw);
        // }
        rw->uctx->slot_table[next_oldest % WRITER_RING_SIZE] = -1;
        rw->uctx->request_type[next_oldest % WRITER_RING_SIZE] = REQ_NONE;
    }

    pthread_mutex_unlock(&rw->lock);

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_write_lock(rwlock_t *rw)
{
    TRACE_PRINT();
    //rwlock_write_lock() should refuse to proceed when qr_waiters > 0 (and/or when readers exist)
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