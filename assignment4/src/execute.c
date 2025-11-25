/*--------------------------------------------------------------------*/
/* execute.c */
/* Author: Jongki Park, Kyoungsoo Park */
/*--------------------------------------------------------------------*/

#include "dynarray.h"
#include "token.h"
#include "util.h"
#include "lexsyn.h"
#include "snush.h"
#include "execute.h"
#include "job.h"

extern struct job_manager *manager;
extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;

/*--------------------------------------------------------------------*/
void block_signal(int sig, int block) { //Temporarily block or unblock a specific signal (like SIGCHLD or SIGINT) for the current process
    //sig: which signal to affect (e.g. SIGCHLD).
    //block: if non-zero → block; if 0 → unblock.
    sigset_t set;           //a signal set object (a bitmask-like structure that can hold multiple signals). it holds signals we want to change
    sigemptyset(&set);      //Initializes set to be empty (no signals in it).
    sigaddset(&set, sig);   //Adds the signal sig to the set.

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) < 0) { //If block != 0 → use SIG_BLOCK (add these signals to the blocked set). Else, use SIG_UNBLOCK (remove these signals from the blocked set).
        fprintf(stderr, 
            "[Error] block_signal: sigprocmask(%s, sig=%d) failed: %s\n",
            block ? "SIG_BLOCK" : "SIG_UNBLOCK", sig, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
void handle_sigchld(void) {

    /*
     * TODO: Implement handle_sigchld() in execute.c
     * Call waitpid() to wait for the child process to terminate.
     * If the child process terminates, handle the job accordingly.
     * Be careful to handle the SIGCHLD signal flag and unblock SIGCHLD.
    */
    
}
/*--------------------------------------------------------------------*/
void handle_sigint(void) {
    
    /*
     * TODO: Implement handle_sigint() in execute.c
     * Find the foreground job and send signal to every process in the
     * process group.
     * Be careful to handle the SIGINT signal flag and unblock SIGINT.
     */
    
}
/*--------------------------------------------------------------------*/
void dup2_e(int oldfd, int newfd, const char *func, const int line) {
    int ret;

    ret = dup2(oldfd, newfd);
    if (ret < 0) {
        fprintf(stderr, 
            "Error dup2(%d, %d): %s(%s) at (%s:%d)\n", 
            oldfd, newfd, strerror(errno), errno_name(errno), func, line);
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
/* Do not modify this function. It is used to check the signals and 
 * handle them accordingly. It is called in the main loop of snush.c.
 */
void check_signals(void) {
    handle_sigchld();
    handle_sigint();
}
/*--------------------------------------------------------------------*/
void redout_handler(char *fname) { // *fname: filename to be used for redirection
    /*
    TODO: Implement redout_handler in execute.c
    */
    int fd;
    fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        error_print(NULL, PERROR);
        exit(EXIT_FAILURE);
    } else {
        dup2_e(fd, STDOUT_FILENO, __func__, __LINE__);
        close(fd);
    }
    
}
/*--------------------------------------------------------------------*/
void redin_handler(char *fname) {
    int fd;

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        error_print(NULL, PERROR);
        exit(EXIT_FAILURE);
    } else {
        dup2_e(fd, STDIN_FILENO, __func__, __LINE__);
        close(fd);
    }
}
/*--------------------------------------------------------------------*/
void build_command_partial(DynArray_T oTokens, int start, 
                        int end, char *args[]) { //translates tokens (parsing result) into an argv array suitable for execvp() + performs IO redirection
    int i, redin = FALSE, redout = FALSE, cnt = 0;
    struct Token *t; //Temp pointer to the current token.

    /* Build command */
    for (i = start; i < end; i++) { //Loop over tokens from index start to end-1.
        t = dynarray_get(oTokens, i);

        if (t->token_type == TOKEN_WORD) { //If the token is a normal word 
            if (redin == TRUE) { //if that file(?) is supposed to be redin
                redin_handler(t->token_value);
                redin = FALSE;
            }
            else if (redout == TRUE) { //if that file(?) is supposed to be redout
                redout_handler(t->token_value);
                redout = FALSE;
            }
            else {
                args[cnt++] = t->token_value; //add token to args
            }
        }
        else if (t->token_type == TOKEN_REDIN)
            redin = TRUE;
        else if (t->token_type == TOKEN_REDOUT)
            redout = TRUE;
    }

    if (cnt >= MAX_ARGS_CNT) 
        fprintf(stderr, "[BUG] args overflow! cnt=%d\n", cnt);

    args[cnt] = NULL;

#ifdef DEBUG
    for (i = 0; i < cnt; i++) {
        if (args[i] == NULL)
            printf("CMD: NULL\n");
        else
            printf("CMD: %s\n", args[i]);
    }
    printf("END\n");
#endif
}
/*--------------------------------------------------------------------*/
void build_command(DynArray_T oTokens, char *args[]) { //convenience wrapper for the common case of “use ALL tokens”
    //Calls build_command_partial starting at index 0 and ending at dynarray_get_length(oTokens) → processing all tokens.
    build_command_partial(oTokens, 0, 
                        dynarray_get_length(oTokens), 
                        args);
}
/*--------------------------------------------------------------------*/
int execute_builtin_partial(DynArray_T toks, int start, int end,
                            enum BuiltinType btype, int in_child) { //Executes a built-in command (cd or exit) for a specific token range from start to end.
    // btype: which builtin it is (B_CD, B_EXIT).
    // in_child: TRUE if this builtin is inside a pipeline (e.g., cd | ls), FALSE if this builtin is in the main shell (no fork)

    int argc = end - start; //number of tokens within this slice.
    struct Token *t1;       //token pointer
    int ret;                // return code from chdir
    char *dir;              // directory string for cd

    switch (btype) {
    case B_EXIT:                //If exit appears inside a pipeline, the child must run the builtin
        if (in_child) return 0; //but in a pipeline, exit must not terminate the shell, only the child process.
        
        if (argc == 1) {        //If the command is exactly exit with no parameters:
            dynarray_map(toks, free_token, NULL); //Free token list, and token array
            dynarray_free(toks);
            exit(EXIT_SUCCESS); //Terminate the entire SNUSH shell 
        }
        else {
            error_print("exit does not take any parameters", FPRINTF);
            return -1;
        }

    case B_CD: { //means command is built-in, should be executed inside parent shell
        if (argc == 1) {
            dir = getenv("HOME");
            if (!dir) {
                error_print("cd: HOME variable not set", FPRINTF);
                return -1;
            }
        } 
        else if (argc == 2) {
            t1 = dynarray_get(toks, start + 1);
            if (t1 && t1->token_type == TOKEN_WORD) 
                dir = t1->token_value;
        } 
        else {
            error_print("cd: Too many parameters", FPRINTF);
            return -1;
        }

        ret = chdir(dir);
        if (ret < 0) {
            error_print(NULL, PERROR);
            return -1;
        }
        return 0;
    }

    default:
        error_print("Bug found in execute_builtin_partial", FPRINTF);
        return -1;
    }
}
/*--------------------------------------------------------------------*/
int execute_builtin(DynArray_T oTokens, enum BuiltinType btype) {
    return execute_builtin_partial(oTokens, 0, 
                                dynarray_get_length(oTokens), btype, FALSE);
}
/*--------------------------------------------------------------------*/
/* 
 * You need to finish implementing job related APIs. (find_job_by_jid(),
 * remove_pid_from_job(), delete_job()) in job.c to handle the job.
 * Feel free to modify the format of the job API according to your design.
 */
void wait_fg(int jobid) { //waits for a foreground job (which may consist of 1 or multiple processes in a pipeline)
    pid_t pid;  //result of waitpid
    int status; //exit status of child

     // Find the job structure by job ID
    struct job *job = find_job_by_jid(jobid);           //Look up the job in the job manager.
    if (!job) {                                         //Error if no such job exists.
        fprintf(stderr, "Job: %d not found\n", jobid);
        return;
    }

    while (1) {
        pid = waitpid(-job->pgid, &status, 0); //wait for any child in the process group pgid.

        if (pid > 0) {
            // Remove the finished process from the job's pid list
            if (!remove_pid_from_job(job, pid)) {
                fprintf(stderr, "Pid %d not found in the job: %d list\n", 
                    pid, job->job_id);
            }

            if (job->remaining_processes == 0) break;
        }

        if (pid == 0) continue;

        if (pid < 0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            error_print("Unknown error waitpid() in wait_fg()", PERROR);
        }
    }

    // Clean up job table entry if all processes are done
    if (job->remaining_processes == 0)
        delete_job(job->job_id);
}
/*--------------------------------------------------------------------*/
void print_job(int jobid, pid_t pgid) {
    fprintf(stdout, 
        "[%d] Process group: %d running in the background\n", jobid, pgid);
}
/*--------------------------------------------------------------------*/
int fork_exec(DynArray_T oTokens, int is_background) { // handles the execution of a single command, which may include I/O redirection
    //oTokens: sequence of tokens parsed from the user's command line input. The entire command line broken up into individual words and symbols
    /*
     * TODO: Implement fork_exec() in execute.c
     * To run a newly forked process in the foreground, call wait_fg() 
     * to wait for the process to finish.  
     * To run it in the background, call print_job() to print job id and
     * process group id.  
     * All terminated processes must be handled by sigchld_handler() in * snush.c. 
     */
     pid_t pid = fork();
     if pid == 


    int jobid = 1;
    return jobid;
}
/*--------------------------------------------------------------------*/
int iter_pipe_fork_exec(int n_pipe, DynArray_T oTokens, int is_background) {
    /*
     * TODO: Implement iter_pipe_fork_exec() in execute.c
     * To run a newly forked process in the foreground, call wait_fg() 
     * to wait for the process to finish.  
     * To run it in the background, call print_job() to print job id and
     * process group id.  
     * All terminated processes must be handled by sigchld_handler() in * snush.c. 
     */

    int jobid = 1;  
    return jobid;
}
/*--------------------------------------------------------------------*/
