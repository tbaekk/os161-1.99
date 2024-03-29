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
#include "opt-A3.h"

#if OPT_A2
#include <mips/trapframe.h>
// a2b
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>

void args_clean(char **args, long idx);
#endif
  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

#if OPT_A3
void sys__exit(int exitcode, bool syscall_safe)
#else
void sys__exit(int exitcode)
#endif
{
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
  //struct proc *cproc = proc_get_from_table_bypid(p->p_id);
  if (p->p_id != PROC_NULL_PID) {
    p->p_state = PROC_ZOMBIE;
#if OPT_A3
    if (syscall_safe) {
      p->p_exitcode = _MKWAIT_EXIT(exitcode);
    }
    else {
      p->p_exitcode = _MKWAIT_SIG(exitcode);
    }
#else
    p->p_exitcode = _MKWAIT_EXIT(exitcode);
#endif
    cv_broadcast(cvWait, procTableLock);
  }
  else {
    p->p_state = PROC_EXITED;
    array_add(reusablePids, &p->p_id, NULL);
  }
  // lock_release(procTableLock);

  // lock_acquire(procTableLock);
  if (p->p_pid != PROC_NULL_PID) {
    // Wait until parent process is finished
    struct proc *par = proc_get_from_table_bypid(p->p_pid);
    if (par != NULL) {
      while (par->p_state == PROC_RUNNING) {
        cv_wait(cvWait, procTableLock);
      }
    }
  }
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
  struct proc *parentProc;
  struct proc *childProc;

  lock_acquire(procTableLock);
  childProc = proc_get_from_table_bypid(pid);

  if (childProc == NULL) {
    DEBUG(DB_SYSCALL, "sys_waitpid: Failed to fetch child process.\n");
    lock_release(procTableLock);
    return ESRCH;
  }

  KASSERT(curproc != NULL);
  parentProc = curproc;

  if (parentProc->p_id != childProc->p_pid) {
    DEBUG(DB_SYSCALL, "sys_waitpid: No related child process.\n");
    return ECHILD;
  }

  while(childProc->p_state == PROC_RUNNING) {
    cv_wait(cvWait, procTableLock);
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
  spinlock_acquire(&childProc->p_lock);
   childProc->p_addrspace = childAddrs;
  spinlock_release(&childProc->p_lock);
  // DEBUG(DB_SYSCALL, "sys_fork: Created addrspace and copied to new process.\n");


  // Assign PID to child process and create the parent/child relationship
  childProc->p_pid = parentProc->p_id;
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

  result = thread_fork(curthread->t_name, childProc, enter_forked_process, ctf, 1);
  if (result) {
    DEBUG(DB_SYSCALL, "sys_fork: Failed to create new thread from thread_fork\n");
    proc_destroy(childProc);
    kfree(ctf);
    ctf = NULL;
    return result;
  }
  DEBUG(DB_SYSCALL, "sys_fork: Created new fork thread\n");

  *retval = childProc->p_id;

  return 0;

}

void args_clean(char **args, long idx) {
  for (int i = 0; i < idx; i++) {
    kfree(args[i]);
  }
}

int sys_execv(const userptr_t program, userptr_t args) {
  int result;
  struct addrspace *as;
  struct vnode *v;
  vaddr_t entrypoint, stackptr;
  char *kprogram;
  char **kargs;

  // Chck if program is empty
  if (program == NULL){
    DEBUG(DB_SYSCALL, "sys_execv: NULL program\n");
    return EFAULT;
  }

  // Count the number of arguments 
  size_t args_num = 0;
  int num=0;
  for(;((char**)args)[num]!=NULL;num++){
    args_num += strlen(((char**)args)[num]);
  }
  if (args_num > ARG_MAX) return E2BIG;

  // Copy the program into the kernel
  int len = strlen((char*)program) + 1;
  kprogram = kmalloc(sizeof(char) * len);
  if (kprogram == NULL)return ENOMEM;
  
  result = copyinstr(program, kprogram, len, NULL);
  if (result) {
    kfree(kprogram);
    return result;
  } 

  // Copy the argumens into the kernel
  kargs = kmalloc(sizeof(char*) * (num + 1));
  if (kargs == NULL) return ENOMEM;

  for (int i = 0; i < num; ++i) {
    kargs[i] = kmalloc(sizeof(char) * (strlen(((char**)args)[i]) + 1));
    if (kargs[i] == NULL) {
      args_clean(kargs,i);
      kfree(kargs);
      return ENOMEM;
    }
    result = copyinstr((userptr_t)((char**)args)[i], kargs[i], strlen(((char**)args)[i])+1, NULL);
    if (result) {
      args_clean(kargs,i);
      kfree(kargs);
      return result;
    }
  }

  /* Open the file. */
  result = vfs_open((char*)program, O_RDONLY, 0, &v);
  if (result) {
    args_clean(kargs, num);
    kfree(kargs);
    kfree(kprogram);
    return result;
  }

  /* We should be a new process. */
  //KASSERT(curproc_getas() == NULL);

  /* Create a new address space. */
  as = as_create();
  if (as == NULL) {
    args_clean(kargs,num);
    kfree(kargs);
    kfree(kprogram);
    vfs_close(v);
    return ENOMEM;
  }

  /* Switch to it and activate it. */
  struct addrspace *oldas = curproc_setas(as);
  as_activate();

  /* Load the executable. */
  result = load_elf(v, &entrypoint);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    args_clean(kargs,num);
    kfree(kargs);
    kfree(kprogram);
    vfs_close(v);
    curproc_setas(oldas);
    return result;
  }

  /* Done with the file now. */
  vfs_close(v);

  /* Define the user stack in the address space */
  result = as_define_stack(as, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    args_clean(kargs,num);
    kfree(kargs);
    kfree(kprogram);
    curproc_setas(oldas);
    return result;
  }

  // Copy args strings onto stack
  size_t adjust = 0;
  vaddr_t argsptr[num+1];
  argsptr[num] = 0;
  for (int i = num - 1; i >= 0; i--) {
    adjust = strlen(kargs[i]) + 1;
    result = copyoutstr(kargs[i], (userptr_t)stackptr-adjust, adjust, NULL);
    if (result) {
      args_clean(kargs,num);
      kfree(kargs);
      kfree(kprogram);
      curproc_setas(oldas);
      return result;
    }
    stackptr -= adjust;
    argsptr[i] = stackptr;
  }

  stackptr = ROUNDUP(stackptr-4, 4);
  for (int i = num; i >= 0; i--) {
    stackptr -= 4;
    result = copyout(&argsptr[i], (userptr_t)stackptr, sizeof(&argsptr[i]));
     if (result) {
      args_clean(kargs,num);
      kfree(kargs);
      kfree(kprogram);
      curproc_setas(oldas);
      return result;
    }
  }

  kfree(oldas);

  enter_new_process(num, (userptr_t)stackptr, stackptr, entrypoint);
  /* enter_new_process does not return. */
  panic("enter_new_process returned\n");
  return EINVAL;
}

#endif
