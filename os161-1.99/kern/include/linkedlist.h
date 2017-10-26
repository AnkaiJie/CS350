#ifndef _LINKEDLIST_H_
#define _LINKEDLIST_H_

// Linked list for integers

struct linkedlist {
	int size;
	struct llnode *head;
};

struct llnode {
	int data;
	struct llnode *next;
};

struct linkedlist *linkedlist_create(void);
bool linkedlist_contains(struct linkedlist* llist, int data);
bool linkedlist_add(struct linkedlist* llist, int data);
bool linkedlist_remove(struct linkedlist* llist, int data);
bool linkedlist_empty(struct linkedlist* llist);
void linkedlist_destroy(struct linkedlist *llist);

#endif /* _LINKEDLIST_H_ */
