// 
// tsh - A tiny shell program with job control
// 
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
//Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
	for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
	/* Parse command line */
	//
	// The 'argv' vector is filled in by the parseline
	// routine below. It provides the arguments needed
	// for the execve() routine, which you'll need to
	// use below to launch a process.
	//
	char *argv[MAXARGS];

	pid_t pid; //process id

	// Signal set to block certain signals
	sigset_t sig_block;          
	
	//
	// The 'bg' variable is TRUE if the job should run
	// in background mode or FALSE if it should run in FG
	//
	int bg = parseline(cmdline, argv); 
	if (argv[0] == NULL)  
		return;   /* ignore empty lines */

	if(!builtin_cmd(argv)){
		// Block SIGCHILD
		// addes SIGCHLD to the sset
		sigaddset(&sig_block, SIGCHLD);             

        // Adds all the signals in set to blocked	
		sigprocmask(SIG_BLOCK, &sig_block, NULL);   

		// Child
        if ((pid = fork()) == 0) {
			// set child's group to a new process group (this is identical 
			// to the child's PID)
            setpgid(0, 0);                              

			//Unblocks SICHLD signal
 			sigprocmask(SIG_UNBLOCK, &sig_block, NULL);                 
            if (execve(argv[0], argv, environ) < 0) {
                printf("%s: Command not found\n", argv[0]);
                exit(0);
            }
        }

		// Parent
		if (!bg){
			if(addjob(jobs, pid, FG, cmdline)){
				sigprocmask(SIG_UNBLOCK, &sig_block, NULL);  
				waitfg(pid);
			}
			else {
				kill(-pid,SIGINT);
			}
		}
		else {
			if(addjob(jobs, pid, BG, cmdline)){
				sigprocmask(SIG_UNBLOCK, &sig_block, NULL);  
				printf("[%d] (%d) %s", pid2jid(pid), (int)pid, cmdline);
			}
			else {
				kill(-pid, SIGINT);
			}
			
		}	
				
	}

	return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
    
	//thank you for casting to c++ string
	//even though it bit me in the ass for later test cases
    if (strcmp(argv[0], "quit") == 0) {
        exit(0);
    }
    else if (strcmp(argv[0], "jobs") == 0) {
        listjobs(jobs);
        return 1;
    }
    else if (strcmp(argv[0], "bg") == 0) {
        do_bgfg(argv);
        return 1;
    }
    else if (strcmp(argv[0], "fg") == 0) {
        do_bgfg(argv);
        return 1;
    }
    
    return 0;     /* not a builtin command */
}
/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
void do_bgfg(char **argv) 
{
    int jid, pid;

	//use c-string (char[]) instead of cast c++ string
	//for easier evaluation fo input 
    char *c_arg = argv[1];
    struct job_t *job;
    
	//if no arguments, ignore, prompt, return
    if (c_arg != NULL) {     
		
		//checks if input is jid
		if (c_arg[0] == '%' && isdigit(c_arg[1])) {	
			
			//atoi converts string to int for proper functionality of
			//getjobid()
			jid = atoi(&c_arg[1]);
			
			if (!(job = getjobjid(jobs, jid))) {
				printf("%s: No such job\n", c_arg);
				return;
			}
			
		} 
		//checks to see if input is pid
		else if (isdigit(*argv[1])) {	
			
			//atoi converts string to int for proper functionality
			//of assignment
			pid = atoi(&c_arg[0]);
			
			if (!(job = getjobpid(jobs, pid))) {
			printf("(%s): No such process\n", argv[1]);
			return;
			}
			
		} 
		else {
			printf("%s: argument must be a PID or %%jobid\n", argv[0]);
			return;
		}
		
    } 
	else {
	printf("%s command requires PID or %%jobid argument\n", argv[0]);
	return;
    }
    
    if (job != NULL) {
	pid = job -> pid;
	
	//if job state is (stopped)
	if (job -> state == 3) { //check if job is stopped
	    if (!strcmp(argv[0], "bg")) {
			printf("[%d] (%d) %s", job -> jid, job -> pid, job -> cmdline);
			job -> state = 2;
			kill(-pid, SIGCONT);
		}
	    
	    if (!strcmp(argv[0], "fg")) {
			job -> state = 1;
			kill(-pid, SIGCONT);
			waitfg(job -> pid);
	    }
	}

	//if job state is (background)	
	if (job -> state == 2) {
	    if (!strcmp(argv[0], "fg")) {
		job -> state = 1;
		waitfg(job -> pid);
	    }
	}
    }
    
    return;
}
/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	while(fgpid(jobs)==pid) {}
		return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
    // In sigchld_handler, use exactly one call to wait pid.
    // Must reap all available zombie children.

    pid_t pid;
    int cur_stat;
    int jid;
    
    // waitpid(pid_t pid, int *cur_stat, int options) funciton info
    // if pid = -1 : then the wait set consists of all of the parent's child process
	//		in this case do nothing
    // WNOHANG | WUNTRACED option :
    // Return immediately, with a return value of 0, if none of the 
	// children in the wait set has stopped or terminated, or with a 
	// return value equal to the PID of one of the stopped or terminated children.
    
    // if pid > 0 means the wait thats set is the child process with same pid
    while((pid = waitpid(-1, &cur_stat, WUNTRACED | WNOHANG)) > 0) {    // Reap a zombie child
        jid = pid2jid(pid);
        
        // Now checking the exit cur_stat of a reaped child
        
        // WIFEXITED returns true if exiting properly
        if (WIFEXITED(cur_stat)) {
            deletejob(jobs, pid); // Delete the child from the job list
        }
        
        // WIFSTOPPED returns true if child that caused the return is 
		// currently stopped.
        else if (WIFSTOPPED(cur_stat)) {     
			getjobpid(jobs, pid) -> state = 3; // Change job cur_stat to ST (stopped)
            printf("Job [%d] (%d) stopped by signal %d\n", jid, (int) pid, WSTOPSIG(cur_stat));
        }

        // WIFSIGNALED returns true if the child process terminated because
		// of a signal that was not caught
		//
        // SIGINT default behavior is terminate
        else if (WIFSIGNALED(cur_stat)) {
            deletejob(jobs,pid);
            printf("Job [%d] (%d) terminated by signal %d\n", jid, (int) pid, WTERMSIG(cur_stat));
        }
        
        
    }
    
    if (verbose) printf("sigchld_handler: exiting\n");
    return;
}


/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
    // be sure to send SIGINT and SIGTSTP signals to the entire foreground pro
	// cess group, using '-pid' instead of 'pid' in the argument to the kill 
	// function. The sdriver.pl program tests for this error.
    
    pid_t pid = fgpid(jobs);
    
    if (pid != 0) {
        // Sends SIGINT to every process in the same process group with pid
        kill(-pid, sig); // signals to ENTIRE foreground process group
    }
    
    return;
}
/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
    // be sure to send SIGINT and SIGTSTP signals to the entire foreground 
	// process group, using '-pid' instead of 'pid' in the argument to the 
	// kill function. The sdriver.pl program tests for this error.
    
    pid_t pid = fgpid(jobs);
    
    if (pid != 0) {
        kill(-pid, sig); // signals to ENTIRE foreground process group
    }
    
    return;
}
/*********************
 * End signal handlers
 *********************/

