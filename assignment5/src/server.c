/*--------------------------------------------------------------------*/
/* server.c                                                           */
/* Author: Junghan Yoon, KyoungSoo Park                               */
/* Modified by: Yebin Pyun                                          */
/*--------------------------------------------------------------------*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/time.h>
#include "common.h"
#include "skvslib.h"
/*--------------------------------------------------------------------*/
struct thread_args // the package you pass into each worker thread via pthread_create
{
    int listenfd; //the shared listening socket
    int idx; //thread number 
    struct skvs_ctx *ctx; //pointer to the global key-value store context

/*--------------------------------------------------------------------*/
    /* free to use */

int sockfd;

/*--------------------------------------------------------------------*/
};
/*--------------------------------------------------------------------*/
volatile static sig_atomic_t g_shutdown = 0;
/*--------------------------------------------------------------------*/
void *handle_client(void *arg)
{
    TRACE_PRINT();
    struct thread_args *args = (struct thread_args *)arg;
    struct skvs_ctx *ctx = args->ctx;
    int idx = args->idx;
    int listenfd = args->listenfd;
/*--------------------------------------------------------------------*/
    /* free to add any variables */
    // int client_sd;
    // struct sockaddr_in server_addr, client_addr;
    // socklen_t client_len;

/*--------------------------------------------------------------------*/

    free(args);
    printf("%dth worker ready\n", idx);

/*--------------------------------------------------------------------*/
    /* edit here */
    while(1){
        //
    }

/*--------------------------------------------------------------------*/

    return NULL;
}
/*--------------------------------------------------------------------*/
/* Signal handler for SIGINT */
void handle_sigint(int sig)
{
    TRACE_PRINT();
    printf("\nReceived SIGINT, initiating shutdown...\n");
    g_shutdown = 1;
}
/*--------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    size_t hash_size = DEFAULT_HASH_SIZE;
    char *ip = DEFAULT_ANY_IP;
    int port = DEFAULT_PORT, opt;
    int num_threads = NUM_THREADS;
    int delay = RWLOCK_DELAY;
/*--------------------------------------------------------------------*/
    /* free to declare any variables */
    struct skvs_ctx *ctx;

/*--------------------------------------------------------------------*/

    /* parse command line options */
    while ((opt = getopt(argc, argv, "p:t:s:d:h")) != -1)
    {
        switch (opt)
        {
        case 'p': //port
            port = atoi(optarg);
            break;
        case 't': //number of worker threads
            num_threads = atoi(optarg);
            break;
        case 's': //hash table size (must be > 0)
            hash_size = atoi(optarg);
            if (hash_size <= 0)
            {
                perror("Invalid hash size");
                exit(EXIT_FAILURE);
            }
            break;
        case 'd': //RW lock delay (used to make lock behavior visible for testing)
            delay = atoi(optarg);
            break;
        case 'h': //prints usage
        default:
            printf("Usage: %s [-p port (%d)] "
                   "[-t num_threads (%d)] "
                   "[-d rwlock_delay (%d)] "
                   "[-s hash_size (%d)]\n",
                   argv[0],
                   DEFAULT_PORT,
                   NUM_THREADS,
                   RWLOCK_DELAY,
                   DEFAULT_HASH_SIZE);
            exit(EXIT_FAILURE);
        }
    }

/*--------------------------------------------------------------------*/
    /* edit here */
    if(signal(SIGINT, handle_sigint) == SIG_ERR){
        perror("Error in SIGINT handling");
        exit(EXIT_FAILURE);
    }
    ctx = skvs_init(hash_size, delay);
    if(ctx == NULL){
        perror("Error in initialising ctx");
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        perror("Error in creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0){
        perror("Error in converting IP from string format to binary network format");
        exit(EXIT_FAILURE);
    }

    int option = 1;
    if (setsockopt(sockfd , SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == -1) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }

    if(bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1){
        perror("Error in binding socket");
        exit(EXIT_FAILURE);
    }

    if(listen(sockfd, NUM_BACKLOG) < 0){
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    //Create worker thread pool
    pthread_t workers[num_threads];
    for(int i=0; i<num_threads; i++){
        struct thread_args *new_args = malloc(sizeof(struct thread_args));
        if(new_args == NULL){
            perror("malloc failed");
            exit(EXIT_FAILURE);
        }
        new_args->listenfd = sockfd;
        new_args->idx = i;
        new_args->ctx = ctx;
        
        if(pthread_create(&workers[i], NULL, handle_client, new_args) != 0){ //0 on success
            perror("Failed to create thread");
            exit(EXIT_FAILURE);
        }
    }

    for(int j=0; j<num_threads; j++){
        pthread_join(workers[j], NULL); //pause the execution of the calling thread until the target thread terminates
        //allows a program to wait for a created thread to complete its task and to retrieve its return value. 
    }

    close(sockfd);
    skvs_destroy(ctx, 1);


/*--------------------------------------------------------------------*/

    return 0;
}
/*--------------------------------------------------------------------*/