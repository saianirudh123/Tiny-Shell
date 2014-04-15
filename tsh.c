/* 
 * tsh - A tiny shell program with job control
 * 
 * Name : AR Sai Anirudh Srinivas
 * ID : 201201197
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/* Extra Created Function */
int typeOfSignal(int);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
	char *argv[MAXARGS];
	char buf[MAXLINE]; 
	int bg;
	pid_t chld_pid;
	
	if (cmdline == 0) // if there is no command entered at command line return
       return;
       
	strcpy(buf, cmdline);// Copies the whole command line eneterd by user with arguments into buf
	bg = parseline(buf, argv);// Parses the  command line and builds argv[] array.
	if (argv[0] == NULL)// If The user hasn't entered in the command line just return
	return;

	if(!(builtin_cmd(argv)))// If the entered command line function/operation is not a buitlin command enters into the if case
	{
		if((chld_pid = fork()) == 0)// Forks a child . by the parent process
		{	
			setpgid(0,0);		
			if(execv(argv[0], argv) < 0)// Calls another method (the child) by using the exec routine
			{
				printf("%s : command line not found\n", argv[0]);// If the command line is not found
				deletejob(jobs, chld_pid);// Delete the allocated job space from the joblist
				kill(getppid(), SIGINT);// Kill the child by sending a SIGINT
				exit(0);
			}
		}
		else
		{	addjob(jobs, chld_pid, bg+1, cmdline);// Executed by the parent prcess
			if(!bg)// If not a backgroud process waits for the operation to get executed 
			{
				waitfg(chld_pid);// By executing waitfg routine
			}
			else
				printf("%d %s", chld_pid, cmdline);	// prints the prompt and the pid with the command line entered	
	return;
		}
	}

	return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(!strcmp(argv[0], "quit")) // for in built quit function
	exit(0);// just exits the program
	
	if(!strcmp(argv[0], "jobs"))// for the inbuilt function jobs
	{
		listjobs(jobs);// List's all the presently active jobs.
		return 1;
	}
	
	if(!strcmp(argv[0], "bg"))// for the command line input bg.
	{
		do_bgfg(argv);// executes the do_bgfg routine
		return 1;
	}
	
	if(!strcmp(argv[0], "fg"))// for the command line input fg.
	{
		do_bgfg(argv);// executes the do_bgfg routine
		return 1;
	}
	
    
	return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{	
	pid_t pid;
	int jid;
	struct job_t *job;// Creates a Structure with data type job_t
		if(!strcmp(argv[0], "fg"))// Checks if the user has enetered fg 
		{
			if(argv[1] != NULL)// the first argument must not be zero
			{
				if(argv[1][0] == '%')// Checks if the user has entered a "%" symbol which specifies that the following number id a job id
				{
					jid = atoi(&argv[1][1]);// converts the string jid to an integer
					if((job = getjobjid(jobs, jid)) == NULL)// Checks for the specified jid in jobs list
					{
						printf("%s : No such job!\n", argv[1]);// If no job is found is found in the job-list
					}
					else
					{
						if(job->state == ST)// Checks if the jobs state is presently stoppped
						{
							job->state = FG;// Converts the state of the job to Foreground
							kill((-1)*(int)job->pid, SIGCONT); // Restarts the job in Foreground by sending a SIGCONT signal to the job
							waitfg(job->pid);// Waits for the job to end before the next command
						}
						else if(job->state == BG)// Checks if the jobs state is presently Background
						{
							job->state = FG;// Converts the state of the job to Foreground
							waitfg(job->pid);// Waits for the job to end before the next command
						}
						else
							printf("job %s already in fg\n", argv[1]);	// Means that the job was already in foreground and hence it didn't satisfy any of the conditions	
					}	
				}	
				else if(isdigit(argv[1][0]))// Checks if the User has entered a digit as the argument
				{
					pid = atoi(&argv[1][0]);// Converts the string number into an integer
					if((job = getjobpid(jobs, pid)) == NULL)// Checks for the specified jid in jobs list
					{
						printf("%s : No such job!\n", argv[1]);// If the specified job is not found
					}
					else
					{
						if(job->state == ST) // Checks if the jobs state is presently stoppped 
						{
							job->state = FG;// Converts the state of the job to Foreground
							kill((-1)*(int)job->pid, SIGCONT); // Restarts the job in Foreground by sending a SIGCONT signal to the job
							waitfg(job->pid);// Waits for the job to end before the next command
							
						}
						else if(job->state == BG)
						{
							job->state = FG;// Converts the state of the job to Foreground
							waitfg(job->pid);// Waits for the job to end before the next command
						}
						else
							printf("job %s already in fg\n", argv[1]);// Means that the job was already in foreground and hence it didn't satisfy any of the conditions	
					}
				}
				else
					printf("arg must be %%jid or pid\n");// If the user doesnt enter a valid jid or pid
			}
			else
				printf("fg command requires PID or %%jobid argument\n");		
		}
		else
		{
			if(argv[1] != NULL)
			{	
				if(argv[1][0] == '%')// Checks if the user has entered a "%" symbol which specifies that the following number id a job id
				{
					jid = atoi(&argv[1][1]);// COnverts the jid from a string form to int
					if((job = getjobjid(jobs, jid)) == NULL)// Checks for the specified jid in jobs list
					{
						printf("%s : No such job!\n", argv[1]);// If no such job with the specified job id is present
					}
					else
					{
						if(job->state == ST) // Checks if the jobs state is presently stoppped 
						{
							job->state = BG;// Changes the state of the job to background
							kill((-1)*(int)job->pid, SIGCONT);// Sends a continue signal using SIGCONT signal
						}
						else
							printf("job %s already in bg\n", argv[1]);	// If the job is already in background	
					}	
				}	
				else if(isdigit(argv[1][0]))// Checks if the user has entered only a digit
				{
					pid = atoi(&argv[1][0]);// Converts the number from string format to integer
					if((job = getjobpid(jobs, pid)) == NULL)// Checks for the specified jid in jobs list
					{
						printf("%s : No such job!\n", argv[1]);// If no such job with the specified pid is present
					}
					else
					{
						if(job->state == ST)// Checks if the jobs state is presently stoppped
						{
							job->state = BG;// Changes the state of the job to background
							kill((-1)*(int)job->pid, SIGCONT);// Sends a continue signal using SIGCONT signal
						}
						else
							printf("job %s already in bg\n", argv[1]);// If the jog is already in background
					}
				}
				else
					printf("arg must be %%jid or pid\n");
			}
			else
				printf("fg command requires PID or %%jobid argument\n");
		}
	return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{	
	struct job_t *job;// Creates a job structure of data type job_t
	while ((job = getjobpid (jobs, pid)) != NULL)// checks the jobs list for the specified pid
        {
	    if (job->state != FG) // Checks If the job state is FG or not
		break; // If yes then breaks from the function
            sleep (1);
        }
  	return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	int stat;// The exit status of that pid will be returned to this variable
	pid_t pid;
	while ((pid = waitpid (-1, &stat, WNOHANG | WUNTRACED)) > 0)// Goes into the loop if atleast one of the child is not yet terminated and 
  // returns its pid
	{
	    struct job_t *job = getjobpid (jobs, pid);// Creates a structure of data type job_t and initializes it with job with the specified pid
	        if (WIFSIGNALED (stat) != 0)// Returns true if the child process terminated because of a signal that was not caught.
	        {
	          deletejob (jobs, pid);// the job is deleted from the joblist
	 	      printf ("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, SIGINT);
      	    }
            else if (WIFSTOPPED (stat) != 0)// Returns true if the child that caused a return is currently stopped
            {
	 	      job->state = ST;// Changes the state of the job to Stopped
         	  printf ("Job [%d] (%d) stopped by signal %d\n", job->jid, pid, SIGTSTP);
            }
      	    else if (WIFEXITED (stat) != 0)// Returns true if the child terminated normally via a call to exit or return
            {
	 	       deletejob (jobs, pid);// the job is deleted from the joblist
            }
       }
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	
	struct job_t *job;// Creates a jobs structure
    pid_t fg_pid = fgpid (jobs);// Returns the pid of the current foreground job
    job = getjobpid (jobs, fg_pid);// Finds a job with id equal to the provided pid
    
    if (kill ((-1) * (int) fg_pid, SIGINT)!= 0)// Sends the SIGINT signal to the job using its pid
		unix_error("Passing SIGINT failed!\n");
    else
    {
	  int c = typeOfSignal(SIGTSTP);
	  if(c == 2)
       printf ("Job [%d] (%d) terminated by signal : Terminated\n ", job->jid, fg_pid);// prints out to the user about the status of the job
    }
   return;
}

/* 
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
   
	struct job_t *job;// Creates a jobs structure
	pid_t fg_pid = fgpid (jobs);// Returns the pid of the current foreground job
    job =  getjobpid (jobs, fg_pid);// Finds a job with id equal to the provided pid
    
     if (kill ((-1) * (int) fg_pid, SIGTSTP)!= 0)// Sends the SIGSTOP signal to the job using its pid
		unix_error("Passing SIGSTOP failed!\n");
	else
	{
	  int c = typeOfSignal(SIGTSTP);// Finds the type of the signal
	  if(c == 1)
       printf ("Job [%d] (%d) stopped by signal : Stopped\n", job->jid, fg_pid);// prints out to the user about the status of the job
    }

    return;
}

int typeOfSignal(int signal)
{
	if(signal == SIGTSTP)
	  return 1;
	if(signal == SIGINT)
	  return 2;
	return 0;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



