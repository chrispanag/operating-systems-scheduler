#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/wait.h>
#include <sys/types.h>

#include "proc-common.h"
#include "request.h"

/* Compile-time parameters. */
#define SCHED_TQ_SEC 2                /* time quantum */
#define TASK_NAME_SZ 60               /* maximum size for a task's name */
#define SHELL_EXECUTABLE_NAME "shell" /* executable for shell */

/* Define Linked List Structure & functions */
typedef struct node {
  int id;
  pid_t pid;
  char* name;
  struct node* next;
  struct node* prev;
} node;

node* newNode(int id, pid_t pid, char* name) {
  node* Node = (node*) malloc(sizeof(node));
  Node->id = id;
  Node->pid = pid;
  // Copy name to the struct
  Node->name = (char*) malloc(sizeof(char) * TASK_NAME_SZ);
  strcpy(Node->name, name);
  return Node;
}

node* addNode(node* list, pid_t pid, char* name) {
  node* head = list;
  if (head == NULL) {
    head = newNode(0, pid, name);
    head->next = head;
    head->prev = head;
  } else {
    while (list->next != head) {
      list = list->next;
    }
    list->next = newNode(list->id + 1, pid, name);
    list->next->next = head;
    list->next->prev = list;
    head->prev = list->next;
  }
  return head;
}

void printList(node* list) {
  if (list != NULL) {
    node* head = list;
    do {
      printf("id: %d\tpid: %d\tname: %s\n", list->id, list->pid, list->name);
      list = list->next;
    } while (list != head);
  }
  printf("\n");
}

node* deleteNode(node* list, pid_t pid, int id) {
  // Search by pid
  node* head = list;
  if (id == -1) {
    if (list->pid == pid) {
      list->next->prev = head->prev;
      head->prev->next = list->next;
      head = list->next;
      free(list);
      return head;
    }
    list = list->next;
    while (list != head) {
      if (list->pid == pid) {
        list->prev->next = list->next;
        list->next->prev = list->prev;
        free(list);
        break;
      }
      list = list->next;
    }
  } else {
    // Search by id
    if (list->id == id) {
      list->next->prev = head->prev;
      head->prev->next = list->next;
      head = list->next;
      free(list);
      return head;
    }
    list = list->next;
    while (list != head) {
      if (list->id == id) {
        list->prev->next = list->next;
        list->next->prev = list->prev;
        free(list);
        break;
      }
      list = list->next;
    }
  }
  return head;
}

node* accessNode(node* list, pid_t pid, int id) {
  node* head = list;
  if (id == -1 && pid >= 0) {
    do {
      if (list->pid == pid) {
        return list;
      }
      list = list->next;
    } while (list != head);
    printf("Error: The node with pid: %d, doesn't exist!\n", pid);
  } else {
    do {
      if (list->id == id) {
        return list;
      }
      list = list->next;
    } while (list != head);
  }
  return NULL;
}

// Define ProcList
node* proc_list_high = NULL;
node* proc_list_low = NULL;
volatile int nproc = 0;

/* Print a list of all tasks currently being scheduled.  */
static void
sched_print_tasks(void)
{
  printf("High:\n");
	printList(proc_list_high);
  printf("Low:\n");
  printList(proc_list_low);
}

/* Send SIGKILL to a task determined by the value of its
 * scheduler-specific id.
 */
static int
sched_kill_task_by_id(int id)
{
  node* stopped = accessNode(proc_list_low, -1, id);
  if (stopped == NULL) stopped = accessNode(proc_list_high, -1, id);
  if (stopped != NULL) {
    kill(stopped->pid, SIGKILL);
    return id;
  }
  printf("Error: The node with id: %d, doesn't exist!\n", id);
  return 0;
}


/* Create a new task.  */
static void
sched_create_task(char *executable)
{
	pid_t pid = fork();
	if (pid < 0) {
		// Error code
		perror("fork");
	} else if (pid == 0) {
		// Child process code
		free(proc_list_low);
    free(proc_list_high);
		char *newargv[] = { executable, NULL, NULL, NULL };
		char *newenviron[] = { NULL };
		raise(SIGSTOP);
		execve(executable, newargv, newenviron);
		// Unreachable point. Execve only returns on error.
		perror("execve");
		exit(1);
	} else {
		// Parent Code
		proc_list_low = addNode(proc_list_low, pid, executable);
		nproc++;  /* number of processes goes here */
	}
}

static void sched_set_priority_high(int id) {
  node* proc = accessNode(proc_list_low, -1, id);
  proc_list_high = addNode(proc_list_high, proc->pid, proc->name);
  proc_list_low = deleteNode(proc_list_low, -1, id);
}

static void sched_set_priority_low(int id) {
  node* proc = accessNode(proc_list_high, -1, id);
  proc_list_low = addNode(proc_list_low, proc->pid, proc->name);
  proc_list_high = deleteNode(proc_list_high, -1, id);
}

/* Process requests by the shell.  */
static int
process_request(struct request_struct *rq)
{
	switch (rq->request_no) {
		case REQ_PRINT_TASKS:
			sched_print_tasks();
			return 0;

		case REQ_KILL_TASK:
			return sched_kill_task_by_id(rq->task_arg);

		case REQ_EXEC_TASK:
			sched_create_task(rq->exec_task_arg);
			return 0;
    case REQ_HIGH_TASK:
      sched_set_priority_high(rq->task_arg);
      return 0;
    case REQ_LOW_TASK:
      sched_set_priority_low(rq->task_arg);
      return 0;

		default:
			return -ENOSYS;
	}
}

/*
 * SIGALRM handler
 */
static void
sigalrm_handler(int signum)
{
  node* list = proc_list_high;
  if (list == NULL) list = proc_list_low;
	kill(list->pid, SIGSTOP);
  alarm(SCHED_TQ_SEC);
}

/*
 * SIGCHLD handler
 */
static void
sigchld_handler(int signum)
{
	signal(SIGALRM, SIG_IGN);
  int status;
  for (;;) {
    pid_t pid = waitpid(-1, &status, WUNTRACED | WNOHANG);
    if (pid < 0) {
      perror("waitpid");
      exit(1);
    }
    if (pid == 0) break;

    //explain_wait_status(pid, status);

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      node* list = proc_list_high;
      if (list == NULL) list = proc_list_low;
      /* A child has died */
      node* stopped = accessNode(list, pid, -1);
      /* Start the next process */
      kill(stopped->next->pid, SIGCONT);
      /* Reset Alarm */
      alarm(SCHED_TQ_SEC);
      /* Delete the killed process from the list */
      list = deleteNode(list, pid, -1);
      if (proc_list_high == NULL) {
        proc_list_low = list;
      } else {
        proc_list_high = list;
      }
      nproc--;
    }
    if (WIFSTOPPED(status)) {
      /* A child has stopped due to SIGSTOP/SIGTSTP, etc... */
      node* list = proc_list_high;
      if (list == NULL) list = proc_list_low;
      list = list->next;
      kill(list->pid, SIGCONT);
      if (proc_list_high == NULL) {
        proc_list_low = list;
      } else {
        proc_list_high = list;
      }
    }
  }
  signal(SIGALRM, sigalrm_handler);
}

/* Disable delivery of SIGALRM and SIGCHLD. */
static void
signals_disable(void)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
		perror("signals_disable: sigprocmask");
		exit(1);
	}
}

/* Enable delivery of SIGALRM and SIGCHLD.  */
static void
signals_enable(void)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
		perror("signals_enable: sigprocmask");
		exit(1);
	}
}


/* Install two signal handlers.
 * One for SIGCHLD, one for SIGALRM.
 * Make sure both signals are masked when one of them is running.
 */
static void
install_signal_handlers(void)
{
	sigset_t sigset;
	struct sigaction sa;

	sa.sa_handler = sigchld_handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	sigaddset(&sigset, SIGALRM);
	sa.sa_mask = sigset;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		perror("sigaction: sigchld");
		exit(1);
	}

	sa.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		perror("sigaction: sigalrm");
		exit(1);
	}

	/*
	 * Ignore SIGPIPE, so that write()s to pipes
	 * with no reader do not result in us being killed,
	 * and write() returns EPIPE instead.
	 */
	if (signal(SIGPIPE, SIG_IGN) < 0) {
		perror("signal: sigpipe");
		exit(1);
	}
}

static void
do_shell(char *executable, int wfd, int rfd)
{
	char arg1[10], arg2[10];
	char *newargv[] = { executable, NULL, NULL, NULL };
	char *newenviron[] = { NULL };

	sprintf(arg1, "%05d", wfd);
	sprintf(arg2, "%05d", rfd);
	newargv[1] = arg1;
	newargv[2] = arg2;

	raise(SIGSTOP);
	execve(executable, newargv, newenviron);

	/* execve() only returns on error */
	perror("scheduler: child: execve");
	exit(1);
}

/* Create a new shell task.
 *
 * The shell gets special treatment:
 * two pipes are created for communication and passed
 * as command-line arguments to the executable.
 */
static pid_t
sched_create_shell(char *executable, int *request_fd, int *return_fd)
{
	pid_t p;
	int pfds_rq[2], pfds_ret[2];

	if (pipe(pfds_rq) < 0 || pipe(pfds_ret) < 0) {
		perror("pipe");
		exit(1);
	}

	p = fork();
	if (p < 0) {
		perror("scheduler: fork");
		exit(1);
	}

	if (p == 0) {
		/* Child */
		close(pfds_rq[0]);
		close(pfds_ret[1]);
		do_shell(executable, pfds_rq[1], pfds_ret[0]);
		assert(0);
	}
	/* Parent */
	close(pfds_rq[1]);
	close(pfds_ret[0]);
	*request_fd = pfds_rq[0];
	*return_fd = pfds_ret[1];
  return p;
}

static void
shell_request_loop(int request_fd, int return_fd)
{
	int ret;
	struct request_struct rq;

	/*
	 * Keep receiving requests from the shell.
	 */
	for (;;) {
		if (read(request_fd, &rq, sizeof(rq)) != sizeof(rq)) {
			perror("scheduler: read from shell");
			fprintf(stderr, "Scheduler: giving up on shell request processing.\n");
			break;
		}

		signals_disable();
		ret = process_request(&rq);
		signals_enable();

		if (write(return_fd, &ret, sizeof(ret)) != sizeof(ret)) {
			perror("scheduler: write to shell");
			fprintf(stderr, "Scheduler: giving up on shell request processing.\n");
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	/* Two file descriptors for communication with the shell */
	static int request_fd, return_fd;

	/* Create the shell. */
	pid_t shell_pid = sched_create_shell(SHELL_EXECUTABLE_NAME, &request_fd, &return_fd);
  proc_list_low = addNode(proc_list_low, shell_pid, SHELL_EXECUTABLE_NAME);
	nproc++;

	/* TODO: add the shell to the scheduler's tasks */

	/*
	 * For each of argv[1] to argv[argc - 1],
	 * create a new child process, add it to the process list.
	 */
	int i;
	for (i = 0; i < argc - 1; i++) {
		sched_create_task(argv[i+1]);
  }

	if (nproc == 0) {
		fprintf(stderr, "Scheduler: No tasks. Exiting...\n");
		exit(1);
	}

	/* Wait for all children to raise SIGSTOP before exec()ing. */
	wait_for_ready_children(nproc);

	/* Install SIGALRM and SIGCHLD handlers. */
	install_signal_handlers();
	// Start the first process and set alarm
	kill(proc_list_low->pid, SIGCONT);
  alarm(SCHED_TQ_SEC);


	shell_request_loop(request_fd, return_fd);

	/* Now that the shell is gone, just loop forever
	 * until we exit from inside a signal handler.
	 */
	 while (1) {
     if (pause() == -1) {
       if (nproc == 0) {
         printf("No processes on the list. Exiting...");
         exit(0);
       }
     }
   }

	/* Unreachable */
	fprintf(stderr, "Internal error: Reached unreachable point\n");
	return 1;
}
