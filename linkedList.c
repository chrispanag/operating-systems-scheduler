#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>

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
  Node->name = name;
  return Node;
}

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

node* deleteNode(node* list, int id) {
  node* head = list;
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
  return head;
}

node* accessNode(node* list, int id) {
  node* head = list;
  do {
    if (list->id == id) {
      return list;
    }
    list = list->next;
  } while (list != head);
  printf("Error: The node with id: %d, doesn't exist!\n", id);
  return NULL;
}


int main (void) {
  int i;
  node* head = NULL;
  for (i = 0; i <= 10; i++) {
    //printf("test\n");
    head = addNode(head, i, 45678, "lol");
  }
  //head = deleteNode(head, 0);
  head = deleteNode(head, 5);
  head = addNode(head, 11, 567, "lol");
  node* Node = accessNode(head, 0);
  printf("%d\n", Node->id);
  printList(head);

  return 0;
}
