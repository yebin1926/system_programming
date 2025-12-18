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
/*--------------------------------------------------------------------*/
};
/*--------------------------------------------------------------------*/
volatile static sig_atomic_t g_shutdown = 0;
int sockfd;
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
    struct sockaddr_in client_addr;

/*--------------------------------------------------------------------*/

    free(args);
    printf("%dth worker ready\n", idx);

/*--------------------------------------------------------------------*/
    /* edit here */
    while(!g_shutdown){ //accept clients until g_shutdown
        socklen_t client_len = sizeof(client_addr);
        int clientfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len); //accept client

        if(clientfd < 0){ //returns non-neg int on success (the fd for newly created socket)
            if (g_shutdown) break;               // exit on shutdown
            if (errno == EINTR) continue;        // interrupted but not shutting down
            perror("Error in accepting client");
            continue;
        }

        int client_closed = 0; //boolean to move onto the next client

        char response_buf[BUF_SIZE];
        int response_len = 0;

        char total_buf[BUF_SIZE];
        int total_used = 0;

        while(!g_shutdown && !client_closed){ //per-client loop
            int line_len = -1;

            for(int i=0; i<total_used; i++){
                if (total_buf[i] == '\n'){
                    line_len = i + 1;
                    break;
                }
            }

            while(line_len <0 && !g_shutdown && !client_closed){ //accumulate the full request line
                char single_buf[512];
                int buf_len = recv(clientfd, single_buf, sizeof(single_buf), 0);

                //scan the total_buf for any '\n'
                if(buf_len == 0) { //if 0, means EOF
                    client_closed = 1;
                    break;
                }
                if(buf_len < 0){ //if -1, means error
                    if (errno == EINTR && !g_shutdown) continue; //retry
                    perror("Error in retrieving request line");
                    client_closed = 1;
                    break;
                }
                
                if(total_used + buf_len > BUF_SIZE){
                    perror("INVALID CMD\n");
                    client_closed = 1;
                    break;
                }

                memcpy(total_buf + total_used, single_buf, buf_len); //append single_buf into total_buf
                total_used += buf_len;

                for(int i=0; i<total_used; i++){
                    if(total_buf[i] == '\n') {
                        line_len = i + 1;
                        break;
                    }
                }
            }
            if(client_closed || g_shutdown) break;

            if(line_len == 1 && total_buf[0] == '\n'){
                client_closed = 1;
                break;
            }
            
            int serve_int = skvs_serve(ctx, total_buf, line_len, response_buf, (long unsigned int*)&response_len);
            if(serve_int < 0){
                // perror("Error in skvs_serve()");
                // exit(EXIT_FAILURE);
                client_closed = 1;
                break;
            } else if(serve_int == 0) continue;  //keep receiving from the same client

            int sent = 0;
            //successful response - send
            while(sent < response_len){
                int n = send(clientfd, response_buf+sent, response_len-sent, 0);
                if(n < 0){
                    if (errno == EINTR && !g_shutdown) continue; //retry
                    perror("Error in sending message");
                    client_closed = 1;
                    break;
                } else if (n == 0){
                    client_closed = 1;
                    break;
                }
                sent += n;
            }

            memmove(total_buf, total_buf + line_len, total_used - line_len); //remove consumed line from total_buf
            total_used -= line_len;
            
        }

        close(clientfd);
    }

/*--------------------------------------------------------------------*/

    return NULL;
}
/*--------------------------------------------------------------------*/
/* Signal handler for SIGINT */
void handle_sigint(int sig)
{
    // TRACE_PRINT();
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

    if(listen(sockfd, NUM_BACKLOG) < 0){ //puts server socket into “ready to accept connections” mode
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

    while (!g_shutdown) {
        pause();   // sleeps until a signal arrives
    }

    //wake up threads stuck in accept()
    shutdown(sockfd, SHUT_RDWR);  // ignore errors if already closed
    close(sockfd);
    sockfd = -1;                  

    for(int j=0; j<num_threads; j++){
        pthread_join(workers[j], NULL); //pause the execution of the calling thread until the target thread terminates
        //allows a program to wait for a created thread to complete its task and to retrieve its return value. 
    }
    
    skvs_destroy(ctx, 1);


/*--------------------------------------------------------------------*/

    return 0;
}
/*--------------------------------------------------------------------*/