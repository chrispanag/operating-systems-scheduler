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

/* Define Linked List Structure & functions */
typedef struct node {
  int id;
  pid_t pid;
  char* name;
  struct node* next;
  struct node* prev;
} node;

/* Allocates memory and initializes a node */
node* newNode(int id, pid_t pid, char* name) {
  node* Node = (node*) malloc(sizeof(node));
  Node->id = id;
  Node->pid = pid;
  Node->name = strdup(name);
  return Node;
}

/* Adds a node to the list */
node* addNode(node* list, int id, pid_t pid, char* name) {
  node* head = list;
  if (head == NULL) {
    head = newNode(id, pid, name);
    head->next = head;
    head->prev = head;
  } else {
    while (list->next != head) {
      list = list->next;
    }
    list->next = newNode(id, pid, name);
    list->next->next = head;
    list->next->prev = list;
    head->prev = list->next;
  }
  return head;
}

void printList(node* list) {
  node* head = list;
  printf("%d ", list->id);
  while (list->next != head) {
    list = list->next;
    printf("%d ", list->id);
  }
  printf("\n");
}

node* deleteNode(node* list, pid_t pid) {
  node* head = list;
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
  return head;
}

node* accessNode(node* list, pid_t pid) {
  node* head = list;
  do {
    if (list->pid == pid) {
      return list;
    }
    list = list->next;
  } while (list != head);
  printf("Error: The node with id: %d, doesn't exist!\n", pid);
  return NULL;
}

// Define ProcList
node* proc_list = NULL;
volatile int nproc = 0;

/*
* SIGALRM handler
*/
static void sigalrm_handler(int signum) {
  kill(proc_list->pid, SIGSTOP);
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
    if (nproc <= 0) break; // If there are no child processes just exit.
    pid_t pid = waitpid(-1, &status, WUNTRACED | WNOHANG); // If one of the children has changed status return pid
    if (pid < 0) {
      perror("waitpid");
      exit(1);
    }
    if (pid == 0) break; // No children have stopped

    explain_wait_status(pid, status);

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      /* A child has died */
      node* stopped = accessNode(proc_list, pid);
      kill(stopped->next->pid, SIGCONT);
      proc_list = deleteNode(proc_list, pid);
      nproc--;
      printf("Parent: Received SIGCHLD, child is dead. Exiting.\n");
    }
    if (WIFSTOPPED(status)) {
      /* A child has stopped due to SIGSTOP/SIGTSTP, etc... */
      printf("Parent: Child has been stopped. Moving right along...\n");
      proc_list = proc_list->next;
      kill(proc_list->pid, SIGCONT);
    }
    /* Reset Alarm */
    alarm(SCHED_TQ_SEC);
  }
  signal(SIGALRM, sigalrm_handler);
}

/* Install two signal handlers.
* One for SIGCHLD, one for SIGALRM.
* Make sure both signals are masked when one of them is running.
*/
static void install_signal_handlers(void) {
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

int main(int argc, char *argv[]) {
  int i;
  /*
  * For each of argv[1] to argv[argc - 1],
  * create a new child process, add it to the process list.
  */
  for (i = 0; i < argc - 1; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      // Error code
      perror("fork");
    } else if (pid == 0) {
      // Child process code
      free(proc_list);  // Delete proc_list from child so that it doesn't have access to other processes

      char *executable = argv[i+1];
      char *newargv[] = { executable, NULL, NULL, NULL };
      char *newenviron[] = { NULL };
      raise(SIGSTOP);
      execve(executable, newargv, newenviron);
      // Unreachable point. Execve only returns on error.
      perror("execve");
      exit(1);
    } else {
      // Parent Code
      proc_list = addNode(proc_list, i, pid, argv[i+1]);
      nproc++;  /* number of proccesses goes here */
    }
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
  kill(proc_list->pid, SIGCONT);
  alarm(SCHED_TQ_SEC);

  /* loop forever  until we exit from inside a signal handler. */
  //while (pause());
  while (1) {
    if (pause() == -1) {
      if (nproc == 0) {
        printf("No processes on the list. Exiting...\n");
        exit(0);
      }
    }
  }

  /* Unreachable */
  fprintf(stderr, "Internal error: Reached unreachable point\n");
  return 1;
}
