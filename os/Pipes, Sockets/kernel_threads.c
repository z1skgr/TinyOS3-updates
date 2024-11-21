#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_threads.h"
#include "kernel_cc.h"

#define PTCB_SIZE (sizeof(PTCB))

/* This is specific to Intel Pentium! */
#define SYSTEM_PAGE_SIZE  (1<<12)


/** 
  @brief Function that starts a new thread.
  */
void start_thread(){  

  int exitval;

  Task call =  CURPTCB->task;
  int argl = CURPTCB->argl;
  void* args = CURPTCB->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
}


/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{

  /* Allocate memory for ptcb. */
  PTCB* ptcb = (PTCB*) xmalloc(sizeof(PTCB));

  /* Init ptcb */
  ptcb->ref_count = 0;
  ptcb->thread_detached = 0;
  ptcb->cv = COND_INIT;
  ptcb->task = task;
  ptcb->argl = argl;
  ptcb->args = args;
  ptcb->thread_exited = 0;

  /* The tid is the address of the ptcb. */
  ptcb->tid = (Tid_t) ptcb;

  /* Make node. */
  rlnode_init(& ptcb->ptcb_node, ptcb);

  /* Connect list and pointers. */
  ptcb->owner_pcb = CURPROC;
  rlist_push_back(& CURPROC->PTCB_list, & ptcb->ptcb_node);

  /* Creating a thread and connecting it to ptcb. */
  if(task != NULL) {
    CURPROC->thread_count++;
    ptcb->thread = spawn_thread(CURPROC, start_thread);
    ptcb->thread->owner_ptcb = ptcb;
    wakeup(ptcb->thread);
  }
	return ptcb->tid;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) CURPTCB;
}


/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
  PTCB* ptcb = (PTCB*)tid;
  //For valid tid.
  if(rlist_find(& CURPROC->PTCB_list, ptcb, NULL) != NULL){
    // If it's not itself, the thread it wants to join is not detached and hasn't joined somewhere else.
    if((tid != sys_ThreadSelf()) && (ptcb->thread_detached == 0)){

      // It goes on the cv list until something wakes it up
      ptcb->ref_count++;
      
      // Sleep
      while(ptcb->thread_exited == 0 && ptcb->thread_detached == 0 )
        kernel_wait(& ptcb->cv,SCHED_USER);

      // The exit value of the joined thread. If NULL, the exit status is not returned.
      if(exitval!=NULL)
        *exitval = ptcb->exitval;

      // Out from the list.
      ptcb->ref_count--; 

      if(ptcb->ref_count == 0 && ptcb->thread_exited == 1){
        
        // Remove from the ptcb list of the process
        rlist_remove(& ptcb->ptcb_node);

        // Release ptcb memory.
        free(ptcb);
      }
      return 0;
    }
    // In case something joins a thread that is exited and needs to be cleaned.
    if(ptcb->ref_count == 0 && ptcb->thread_exited == 1){

      // Remove from the ptcb list of the process
      rlist_remove(& ptcb->ptcb_node);

      // Release ptcb memory.
      free(ptcb);
    }  
  }
	return -1;
}


/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb = (PTCB*)tid;

  // Wakes up the thread waiting for it and makes it detached
  if(rlist_find(& CURPROC->PTCB_list, ptcb, NULL) != NULL){
    ptcb->thread_detached = 1;
    Cond_Broadcast(& ptcb->cv);
    return 0;
  }
	return -1;
}


/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  CURPTCB->exitval = exitval;

  // Wake up as many threads waiting for what we're going to kill
  Cond_Broadcast(& CURPTCB->cv);

  CURPROC->thread_count--;

  /* Kill the thread. */
  kernel_sleep(EXITED, SCHED_USER);
}