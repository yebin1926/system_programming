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
    for (int i=0; i < job->remaining_processes; i++){
        if(job->pids[i] == pid){
            for(int j=i; j < (job->remaining_processes-1); j++){
                job->pids[j] = job->pids[j+1];
            }
            job->remaining_processes--;
            return 1;
        }
    }
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int jobid) {
    /*
     * TODO: Implement delete_job()
     */
    struct job job_to_delete = find_job_by_jid(jobid);

    return 0;
}
/*--------------------------------------------------------------------*/
/*
 * TODO: Implement any necessary job-control code in job.c 
 */

int add_job(int jobid){
    //
}

int add_pid_to_job(struct job *job, pid_t pid){
    //
}

struct job *find_job_by_pid(pid_t pid){
    //
}