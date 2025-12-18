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
    int next_element;
    int oldest_element;

    int qr_waiters; //how many quick readers are currently waiting because a writer is active
    int read_batch_end; //the end of the currently granted “reader batch”.

    req_type_t request_type[WRITER_RING_SIZE]; //array mapping each element to its request type
    int slot_table[WRITER_RING_SIZE]; //stores the exact ticket number currently occupying each ring slot
};

/*------------------------Helper Functinos-----------------------------*/
static int slot_matches(struct uctx *u, int num, int req_type){
    int idx = num % WRITER_RING_SIZE;
    if(req_type == -1){
        return (u->slot_table[idx] == num);
    }
    return (u->slot_table[idx] == num && u->request_type[idx] == req_type);
}

static req_type_t get_request_type(struct uctx *u, int num){
    return u->request_type[num % WRITER_RING_SIZE];
}

static int consume_entry(rwlock_t *rw, int num){
    struct uctx *uctx = (struct uctx *)rw->uctx;
    if(!slot_matches(uctx, num, -1)){
        return 0;
    }
    uctx->slot_table[num % WRITER_RING_SIZE] = -1;
    uctx->request_type[num % WRITER_RING_SIZE] = REQ_NONE;
    return 1;
}

static int join_fifo(rwlock_t *rw, int num, int req_type){
    struct uctx *uctx = (struct uctx *)rw->uctx;
    uctx->next_element++;
    uctx->slot_table[num % WRITER_RING_SIZE] = num;
    if(req_type == 1) {uctx->request_type[num % WRITER_RING_SIZE] = REQ_READ;}
    else if(req_type == 2) {uctx->request_type[num % WRITER_RING_SIZE] = REQ_WRITE;}
    else return 0;
    return 1;
}

static int find_valid_head(rwlock_t *rw, int head){
    struct uctx *uctx = (struct uctx *)rw->uctx;
    while(head < uctx->next_element && !slot_matches(uctx, head, -1)){ //move head to the right position
        head++;
    }
    return head;
}

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
        uctx->request_type[i] = REQ_NONE;
        uctx->slot_table[i] = -1;
    }

    rw->uctx = uctx;

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_read_lock(rwlock_t *rw, int quick) //used right before thread starts reading smth
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    if(rw == NULL){
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&rw->lock);

    struct uctx *uctx = (struct uctx *)rw->uctx;

    if(quick){ //QREAD mode
        if(rw->current_writers == 0) {//there are no existing writers
            rw->current_readers++; 
            pthread_mutex_unlock(&rw->lock);
            return 0;
        }
        //else, there are existing writers
        uctx->qr_waiters++;

        while(rw->current_writers > 0){ //there is a writer
            pthread_cond_wait(&uctx->cv, &rw->lock);
        }
        uctx->qr_waiters--;
        rw->current_readers++;

        pthread_cond_broadcast(&uctx->cv);
        pthread_mutex_unlock(&rw->lock);
        return 0; 

    } else { //Normal read mode
        int my_turn = uctx->next_element; //join the fifo
        if(!join_fifo(rw, my_turn, REQ_READ)){
            return -1;
        }
        // rw->uctx->next_element++;
        // rw->uctx->slot_table[my_turn % WRITER_RING_SIZE] = my_turn;
        // rw->uctx->request_type[my_turn % WRITER_RING_SIZE] = REQ_READ;

        while(1){
            //if active writer exists, wait
            if(rw->current_writers > 0){
                pthread_cond_wait(&uctx->cv, &rw->lock);
                continue;
            }
            //if this reader is in reader batch, proceed
            if(my_turn <= uctx->read_batch_end && rw->current_writers == 0){
                rw->current_readers++;
                if(!consume_entry(rw, my_turn)) {
                    pthread_mutex_unlock(&rw->lock);
                    return -1;
                };
                pthread_mutex_unlock(&rw->lock);
                return 0; 
            }

            //if no batch is active, repair the head pointer and check if it's the head
            int head = uctx->oldest_element;
            // while(head < rw->uctx->next_element && !slot_matches(rw->uctx, head, -1)){ //move head to the right position
            //     head++;
            // }
            head = find_valid_head(rw, head);
            if(head == uctx->next_element){ ///if head is the next element, list is empty
                pthread_cond_wait(&uctx->cv, &rw->lock);
                continue;
            }
            uctx->oldest_element = head;

            if(my_turn == head && get_request_type(uctx, head) == REQ_READ){ //if this one IS the head and is READ,
                uctx->read_batch_end = my_turn;
                //compute and update the last element in batch
                while(slot_matches(uctx, head, REQ_READ)){
                    uctx->read_batch_end = head;
                    head++;
                }
                uctx->oldest_element = head;
                rw->current_readers++;
                if(!consume_entry(rw, my_turn)) { //consume the current entry now
                    pthread_mutex_unlock(&rw->lock);
                    return -1;
                }

                pthread_cond_broadcast(&uctx->cv);
                pthread_mutex_unlock(&rw->lock);
                return 0;
            }

            //otherwise, not allowed yet
            pthread_cond_wait(&uctx->cv, &rw->lock);
        }
    }

/*--------------------------------------------------------------------*/
    pthread_mutex_unlock(&rw->lock);
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_read_unlock(rwlock_t *rw) //used right after thread finishes reading smth
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
    struct uctx *uctx = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);
    rw->current_readers--;
    if(rw->current_readers < 0){ //if negative value, someone called read_unlock without holding a read lock.
        pthread_mutex_unlock(&rw->lock);
        return -1;
    }
    if(rw->current_readers > 0){ //other active readers still hold the block
        pthread_mutex_unlock(&rw->lock);
        return 0;
    }
    if(rw->current_readers == 0){ //if there are no active readers, signal oldest element in fifo
        uctx->read_batch_end = -1;
        pthread_cond_broadcast(&uctx->cv);
    }

    pthread_mutex_unlock(&rw->lock);

/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_write_lock(rwlock_t *rw) //used right before thread starts writing smth
{
    TRACE_PRINT();
    //rwlock_write_lock() should refuse to proceed when qr_waiters > 0 (and/or when readers exist)
/*--------------------------------------------------------------------*/
    /* edit here */
    if(rw == NULL){
        errno = EINVAL;
        return -1;
    }
    struct uctx *uctx = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);

    //TODO: Join FIFO and get a ticket
    int my_turn = uctx->next_element;
    if(!join_fifo(rw, my_turn, REQ_WRITE)) {
        pthread_mutex_unlock(&rw->lock);
        return -1;
    }

    while(1){
        //TODO: check if there are any active RWs or a QR If so -> wait.
        if(rw->current_readers > 0 || rw->current_writers > 0 || uctx->qr_waiters > 0){
            pthread_cond_wait(&uctx->cv, &rw->lock);
            continue;
        }

        //TODO: Repair the head pointer
        int head = uctx->oldest_element;
        // while(head < rw->uctx->next_element && !slot_matches(rw->uctx, head, -1)){ //move head to the right position
        //     head++;
        // }
        head = find_valid_head(rw, head);
        uctx->oldest_element = head;

        if(head == uctx->next_element){ ///if head is the next element, list is empty
            perror("current writer was not inserted to the fifo correctly");
            return -1;
        }

        //TODO: If this is the head & writer, activate this writer and block others
        if(my_turn == head && slot_matches(uctx, my_turn, REQ_WRITE)){
            rw->current_writers = 1;
            if(!consume_entry(rw, my_turn)){
                pthread_mutex_unlock(&rw->lock);
                return -1;
            }
            uctx->oldest_element++;
            uctx->oldest_element = find_valid_head(rw, uctx->oldest_element);
            uctx->read_batch_end = -1;
            pthread_mutex_unlock(&rw->lock);
            return 0;
        }
        //if this != head, wait and retry;
        pthread_cond_wait(&uctx->cv, &rw->lock);
    }

/*--------------------------------------------------------------------*/
    pthread_mutex_unlock(&rw->lock);
    return -1;
}
/*--------------------------------------------------------------------*/
int rwlock_write_unlock(rwlock_t *rw) //used right after thread finishes writing smth
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
    struct uctx *uctx = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);
    rw->current_writers--;
    uctx->read_batch_end = -1;

    if(rw->current_writers > 0 || rw->current_writers < 0){ //if there are still writers left, error.
        pthread_cond_broadcast(&uctx->cv);
        pthread_mutex_unlock(&rw->lock);
        return -1;
    }
    pthread_cond_broadcast(&uctx->cv);
    pthread_mutex_unlock(&rw->lock);
    
/*--------------------------------------------------------------------*/
    return 0;
}
/*--------------------------------------------------------------------*/
int rwlock_destroy(rwlock_t *rw)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    /* edit here */
    if (!rw) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&rw->lock);

    struct uctx *u_copy = (struct uctx *)rw->uctx;

    //TODO: make sure lock is not in use
    if(rw->current_readers > 0 || rw->current_writers > 0 || u_copy->qr_waiters > 0 || u_copy->oldest_element != u_copy->next_element){
        pthread_mutex_unlock(&rw->lock);
        errno = EBUSY;
        return -1;
    }

    pthread_mutex_unlock(&rw->lock);
    pthread_cond_destroy(&u_copy->cv);
    pthread_mutex_destroy(&rw->lock);

    free(u_copy);
/*--------------------------------------------------------------------*/

    return 0;
}