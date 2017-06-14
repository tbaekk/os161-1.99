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
#include <synch.h>
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

#if OPT_A2
  lock_acquire(procTableLock);
  p->p_exitcode = _MKWAIT_EXIT(exitcode);
  p->p_state = PROC_EXITED;
  cv_broadcast(p->p_cv, procTableLock);
  lock_release(procTableLock);
#else
  (void)exitcode;
#endif

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
#if OPT_A2
  KASSERT(curproc != NULL);
  *retval = curproc->p_id;
#else
  *retval = 1;
#endif
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

#if OPT_A2
  
  struct proc *parentProc = curproc;
  

  lock_acquire(procTableLock);
  struct proc *childProc = proc_get_from_table_bypid(pid);

  if (childProc == NULL) {
    DEBUG(DB_SYSCALL, "sys_waitpid: Failed to fetch child process.\n");
    lock_release(procTableLock);
    return ESRCH;
  }

  if (childProc->p_parentproc != parentProc) {
    DEBUG(DB_SYSCALL, "sys_waitpid: No related child process.\n");
    return ECHILD;
  }

  while(childProc->p_state == PROC_RUNNING) {
    cv_wait(childProc->p_cv, procTableLock);
  }

  exitstatus = childProc->p_exitcode;
  lock_release(procTableLock);  
#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
#endif

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
  struct proc *parentProc= curproc;
  struct proc *childProc = proc_create_runprogram(parentProc->p_name);
  if (childProc == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create new process.\n");
    return ENOMEM;
  }
  if (childProc->p_id == PROC_NULL_PID) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to assign pid.\n");
    return ENPROC;
  }
  DEBUG(DB_SYSCALL, "sys_fork: Created new process.\n");


  // Create and copy address space (and data) from parent to child
  struct addrspace *childAddrs = as_create();
  if (childAddrs == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create addrspace for new process.\n");
    as_destroy(childAddrs);
    proc_destroy(childProc);
    return ENOMEM;
  }
  result = as_copy(parentProc->p_addrspace, &childAddrs);
  if (result) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to copy addrspace to new process.\n");
    proc_destroy(childProc);
    as_destroy(childAddrs);
    return ENOMEM;
  }

  // Attach the newly created address space to the child process structure
  childProc->p_addrspace = childAddrs;
  DEBUG(DB_SYSCALL, "sys_fork: Created addrspace and copied to new process.\n");


  // Assign PID to child process and create the parent/child relationship
  childProc->p_parentproc = parentProc;
  DEBUG(DB_SYSCALL, "sys_fork: Assigned parent/child relationship.\n");


  /* Create thread for child process.
   * Child thread needs to put the trapframe onto its stack and
   * modify it so that it returns the current value.
   */
  struct trapframe *ctf = kmalloc(sizeof(struct trapframe));
  if (ctf == NULL) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create trapframe for new process.\n");
    proc_destroy(childProc);
    return ENOMEM;
  }
  memcpy(ctf,ptf, sizeof(struct trapframe));
  DEBUG(DB_SYSCALL, "sys_fork: Created new trapframe\n");

  result = thread_fork(curthread->t_name, childProc, enter_forked_process, ctf, 0);
  if (result) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create new thread from thread_fork\n");
    kfree(ctf);
    as_destroy(childAddrs);
    proc_destroy(childProc);
    return result;
  }
  DEBUG(DB_SYSCALL, "sys_fork: Created new fork thread\n");

  *retval = childProc->p_id;

  return 0;

}
#endif
