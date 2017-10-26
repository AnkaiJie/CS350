#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <linkedlist.h>


struct llnode *llnode_create(int data) ;


struct llnode *llnode_create(int data) {
	struct llnode *ll = kmalloc(sizeof(struct llnode));
	if (ll == NULL) return NULL;
	ll->data = data;
	ll->next = NULL;
	return ll;
}


struct linkedlist *linkedlist_create() {
	struct linkedlist *ll = kmalloc(sizeof(struct linkedlist));
	if (ll == NULL) return NULL;
	ll->size = 0;
	ll->head = NULL;
	return ll;

}

bool linkedlist_contains(struct linkedlist* llist, int data) {
	struct llnode *cur = llist->head;
	while(cur != NULL) {
		if (cur->data == data) {
			return true;
		} else {
			cur = cur->next;
		}
	}
	return false;
}

bool linkedlist_add(struct linkedlist* llist, int data) {
	struct llnode *cur = llist->head;
	struct llnode *ll = llnode_create(data);
	if (ll == NULL) return false;
	if (cur == NULL) {
		llist->head = ll;
	} else {
		struct llnode *curnext = cur->next;
		while (curnext != NULL) {
			cur = curnext;
			curnext = curnext->next;
		}

		cur->next = ll;
	}
	llist->size += 1;
	return true;
};

// returns true if data was found in list
bool linkedlist_remove(struct linkedlist* llist, int data) {
	struct llnode *cur = llist->head;
	if (cur == NULL) {
		return false;
	}
	if (cur->data == data) {
		llist->head = cur->next;
		kfree(cur);
		llist->size -= 1;
		return true;
	} else {
		struct llnode *curnext = cur->next;
		while (curnext != NULL) {
			if (curnext->data == data) {
				cur->next = curnext->next;
				kfree(curnext);
				llist->size -= 1;
				return true;
			} else {
				cur = curnext;
				curnext = curnext->next;
			}
		}
	}

	return false;
	
};
bool linkedlist_empty(struct linkedlist* llist) {
	return llist->size == 0;
}

void linkedlist_destroy(struct linkedlist *llist) {
	struct llnode *cur = llist->head;
	if (cur != NULL) {
		struct llnode *next = cur->next;
		while(next != NULL) {
			cur->next = next->next;
			kfree(next);
			next = cur->next;
		}
		kfree(cur);
	}
	kfree(llist);
	return;
}

