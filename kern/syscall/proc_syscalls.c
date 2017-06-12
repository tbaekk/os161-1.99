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
#endif
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

#if OPT_A2
// fork() system call handler
int sys_fork(struct trapframe *ptf, pid_t *retval) {
  KASSERT(curproc != NULL);
  int result;

  // Create process structure for child process
  struct proc *parentProc;
  parentProc = curproc;
  struct proc *childProc;
  childProc = proc_create_runprogram(parentProc->p_name);
  if (childProc == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create new process.\n");
    return ENPROC;
  }
  if (childProc->p_id == PROC_NULL_PID) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to assign pid.\n");
    return ENOMEM;
  }
  DEBUG(DB_SYSCALL, "sys_fork: Created new process.\n");


  // Create and copy address space (and data) from parent to child
  struct addrspace *parentAddrs;
  parentAddrs = curproc_getas();
  if (parentAddrs == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: No address space setup.\n");
    return EFAULT;
  }
  struct addrspace *childAddrs;
  childAddrs = as_create();
  if (childAddrs == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create addrspace for new process.\n");
    as_destroy(childAddrs);
    proc_destroy(childProc);
    return ENOMEM;
  }
  result = as_copy(parentAddrs, &childAddrs);
  if (result) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to copy addrspace to new process.\n");
    proc_destroy(childProc);
    return ENOMEM;
  }

  // Attach the newly created address space to the child process structure
  spinlock_acquire(&childProc->p_lock);
  childProc->p_addrspace = childAddrs;
  spinlock_release(&childProc->p_lock);
  DEBUG(DB_SYSCALL, "sys_fork: Created addrspace and copied to new process.\n");


  // Assign PID to child process and create the parent/child relationship
  childProc->p_pid = parentProc->p_id;
  DEBUG(DB_SYSCALL, "sys_fork: Created parent/child relationship.\n");


  /* Create thread for child process.
   * Child thread needs to put the trapframe onto its stack and
   * modify it so that it returns the current value.
   */
  struct trapframe *ctf;
  ctf = kmalloc(sizeof(struct trapframe));
  if (ctf == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create trapframe for new process.\n");
    proc_destroy(childProc);
    return ENOMEM;
  }
  memcpy(ptf,ctf, sizeof(struct trapframe));
  DEBUG(DB_SYSCALL, "sys_fork: Created new trapframe\n");

  result = thread_fork(curthread->t_name,childProc,enter_forked_process,ctf,1);
  if (result) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create new thread from thread_fork\n");
    kfree(ctf);
    ctf = NULL;
    proc_destroy(childProc);
    return result;
  }
  DEBUG(DB_SYSCALL, "sys_fork: Created new fork thread\n");

  *retval = childProc->p_id;

  return 0;

}
#endif
