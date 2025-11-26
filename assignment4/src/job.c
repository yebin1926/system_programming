/*--------------------------------------------------------------------*/
/* job.c */
/* Author: Jongki Park, Kyoungsoo Park */
/*--------------------------------------------------------------------*/

#include "job.h"

extern struct job_manager *manager;
/*--------------------------------------------------------------------*/
void init_job_manager() {
    manager = (struct job_manager *)calloc(1, sizeof(struct job_manager));
    if (manager == NULL) {
        fprintf(stderr, "[Error] job manager allocation failed\n");
        exit(EXIT_FAILURE);
    }
    /* TODO: Init job manager */
    manager->n_jobs = 0;
    manager->jobs = calloc(MAX_JOBS, sizeof(struct job));
    if (manager->jobs == NULL) {
        fprintf(stderr, "[Error] job list allocation failed\n");
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
/* This is just a placeholder for compilation. You can modify it if you want. */
struct job *find_job_by_jid(int job_id) {
    /*
     * TODO: Implement find_job_by_jid()
     */
    for (int i = 0; i < (manager->n_jobs) ; i++) {
        if(manager->jobs[i].job_id == job_id){
            return &manager->jobs[i];
        }
    }
    return NULL;
}
/*--------------------------------------------------------------------*/
int remove_pid_from_job(struct job *job, pid_t pid) {
    /*
     * TODO: Implement remove_pid_from_job()
    */
    if (!job) return 0;
    block_signal(SIGCHLD, TRUE); //Block SIGCHLD before modifying job structures
    for (int i=0; i < job->remaining_processes; i++){ //for every pid in that job,
        if(job->pids[i] == pid){                      //if its pid == pid,
            for(int j=i; j < (job->remaining_processes-1); j++){    //remove pid by shifting all pids after it to the left
                job->pids[j] = job->pids[j+1];
            }
            job->remaining_processes--;
            block_signal(SIGCHLD, FALSE); //Unblock SIGCHLD after mutating shared data is done
            return 1;
        }
    }
    block_signal(SIGCHLD, FALSE); //unblock SIGCHLD even if pid not found
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int jobid) {
    /*
     * TODO: Implement delete_job()
     */
    block_signal(SIGCHLD, TRUE);

    if (manager == NULL || manager->jobs == NULL || manager->n_jobs == 0) {
        block_signal(SIGCHLD, FALSE);
        return 0;
    }

    for (int i = 0; i < (manager->n_jobs) ; i++) {  //delete the job from job manager
        if(manager->jobs[i].job_id == jobid){
            manager->jobs[i] = manager->jobs[manager->n_jobs-1];
            manager->n_jobs--;
            block_signal(SIGCHLD, FALSE);
            return 1;
        }
    }
    block_signal(SIGCHLD, FALSE);
    return 0;
}
/*--------------------------------------------------------------------*/
/*
 * TODO: Implement any necessary job-control code in job.c 
 */

int add_job(int jobid){
    block_signal(SIGCHLD, TRUE);
    struct job new_job;

    manager->jobs[n_jobs] = 
    manager->n_jobs++;
    block_signal(SIGCHLD, FALSE);
}

int add_pid_to_job(struct job *job, pid_t pid){
    //
}

struct job *find_job_by_pid(pid_t pid){
    //
}