#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include "opt-A2.h"


#if OPT_A2
#include <mips/trapframe.h>
#include <synch.h>

void thread_fork_entry(void *trap, unsigned long arg);



/* Enter user mode. Does not return. */
// void enter_new_process(int argc, userptr_t argv, vaddr_t stackptr,
//            vaddr_t entrypoint);


void thread_fork_entry(void *trap, unsigned long arg) {
  enter_forked_process((struct trapframe *) trap);
  (void)arg;
}



int sys_fork(struct trapframe *tf, pid_t *retval) {
  // child name
  char *curpname = curproc->p_name;
  size_t curpnamelen = strlen(curpname);
  char *childpname = (char *)kmalloc(curpnamelen + sizeof("_child"));
  strcpy(childpname, curpname);
  strcat(childpname, "_child");

  struct proc *child = proc_create_runprogram(childpname);
  if (child == NULL) {
    // out of memory
    *retval = -1;
    return 3;
  }

  // kprintf("\nProcess name: %s and pid: %d\n",curpname, curproc->pid);
  // kprintf("Child process name: %s and pid: %d\n", childpname, child->pid);

  struct addrspace *parentAddrSpace = curproc->p_addrspace;
  struct addrspace *childAddrSpace;

  int copyerr = as_copy(parentAddrSpace, &childAddrSpace);
  // any issues with as_copy
  if (copyerr != 0) {
    *retval = -1;
    return copyerr;
  }

  // Sets address space to child process
  spinlock_acquire(&(child->p_lock));
  child->p_addrspace = childAddrSpace;
  spinlock_release(&(child->p_lock));

  // copy trap frame to heap
  struct trapframe *tfcopy = (struct trapframe *)kmalloc(sizeof(struct trapframe));
  memcpy(tfcopy, tf, sizeof(struct trapframe));

  // know your daddy
  child->parentPid = curproc->pid;

  int tforkerr = thread_fork("fork process thread", child, thread_fork_entry, tfcopy, 0);
  if (tforkerr != 0) {
    *retval = -1;
    return tforkerr;
  }

  *retval = child->pid;
  linkedlist_add(curproc->children, child->pid);

  return 0;
}


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  // pid_t pid = p->pid;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  // kprintf("\nDestroying process: %s\n", curproc->p_name);
  proc_remthread(curthread);

  // this removes any zombie children 
  struct linkedlist *children = p->children;
  struct llnode *cur = children->head;
  struct llnode *prev = NULL;
  while (cur != NULL) {
    struct proc *curChild = get_proc(cur->data);

    struct lock *curparentLock = curChild->parentLock;

    lock_acquire(curparentLock);
    if (prev == NULL && curChild->zombie) {
      children->head = cur->next;
      proc_destroy(curChild);
      cur = children->head;
    } else if (curChild->zombie) {
      prev->next = cur->next;
      proc_destroy(curChild);
      cur = prev->next;
    } else {
      curChild->parentPid = -1;
      prev = cur;
      cur = cur->next;
    }
    lock_release(curparentLock);
  }

  struct lock *parentLock = p->parentLock;

  lock_acquire(parentLock);
  if (p->parentPid == -1 || p->parentPid == 0) {
    //if no parent then we can just destroy
    lock_release(parentLock);
    proc_destroy(p);
  } else {
    //otherwise, become a zombie
    struct lock *exitlock = p->exitLock;
    struct cv *exitcv = p->exitCv;
    
    lock_acquire(exitlock);
    p->zombie = true;
    p->exitRetval = _MKWAIT_EXIT(exitcode);
    cv_signal(exitcv, exitlock);

    lock_release(exitlock);
    lock_release(parentLock);
  }

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  *retval = curproc->pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
      userptr_t status,
      int options,
      pid_t *retval)
{
  int exitstatus;
  int result;

  if (options != 0) {
    return(EINVAL);
  }

  // if child not dead then wait until it is
  struct proc *child = get_proc(pid);
  struct linkedlist *children = curproc->children;

  lock_acquire(child->exitLock);
  
  if (!(child->zombie)) {
    cv_wait(child->exitCv, child->exitLock);
  }

  exitstatus = child->exitRetval;
  // kprintf("EXIT STATUS: %d", exitstatus);
  KASSERT(exitstatus != -1);

  lock_release(child->exitLock);

  //destroy child after
  proc_destroy(child);
  linkedlist_remove(children, pid);


  result = copyout((void *)&exitstatus,status,sizeof(int));
  result = result;
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}



#else
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
#endif
