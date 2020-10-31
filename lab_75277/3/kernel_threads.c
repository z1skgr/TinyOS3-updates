#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_threads.h"
#include "kernel_cc.h"

#define PTCB_SIZE (sizeof(PTCB))

/* This is specific to Intel Pentium! */
#define SYSTEM_PAGE_SIZE  (1<<12)


/** 
  @brief H sunartisi pou kskeinaei ena neo thread.
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

  /* Desmeuoume xwro gia ptcb. */
  PTCB* ptcb = (PTCB*) xmalloc(sizeof(PTCB));

  /* Init ptcb */
  ptcb->ref_count = 0;
  ptcb->thread_detached = 0;
  ptcb->cv = COND_INIT;
  ptcb->task = task;
  ptcb->argl = argl;
  ptcb->args = args;
  ptcb->thread_exited = 0;

  /* To tid einai h dieu8unsh tou ptcb. */
  ptcb->tid = (Tid_t) ptcb;

  /* Dhmiourgia kombou. */
  rlnode_init(& ptcb->ptcb_node, ptcb);

  /* sundesh listwn kai pointers. */
  ptcb->owner_pcb = CURPROC;
  rlist_push_back(& CURPROC->PTCB_list, & ptcb->ptcb_node);

  /* Dhmiourgia thread kai sundesh tou me to ptcb. */
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
  //an iparxei egkiro tid.
  if(rlist_find(& CURPROC->PTCB_list, ptcb, NULL) != NULL){
    // An den einai o euatos tou, den einai detached to thread to opio 8elei na kanei join, kai den exei kanei kapou allou join
    if((tid != sys_ThreadSelf()) && (ptcb->thread_detached == 0) && (CURTHREAD->state != STOPPED)){

      // Mpenei sthn lista tou cv mexri na to ksipnisei kapoios allos
      ptcb->ref_count++;
      
      // Sleep
      while(ptcb->thread_exited == 0 && ptcb->thread_detached == 0 )
        kernel_wait(& ptcb->cv,SCHED_USER);

      // The exit value of the joined thread. If NULL, the exit status is not returned.
      if(exitval!=NULL)
        *exitval = ptcb->exitval;

      // Bghke apo th lista.
      ptcb->ref_count--; 

      if(ptcb->ref_count == 0 && ptcb->thread_exited == 1){
        
        // Afairoume apo thn lista me ta ptcb tou process.
        rlist_remove(& ptcb->ptcb_node);

        // Eleu8erwsh mnhmhs tou ptcb.
        free(ptcb);
      }
      return 0;
    }
    // Se periptwsh pou kanei kapios join se thread to opio einai exited kai prepei na ka8aristei.
    if(ptcb->ref_count == 0 && ptcb->thread_exited == 1){

      // Afairoume apo thn lista me ta ptcb tou process.
      rlist_remove(& ptcb->ptcb_node);

      // Eleu8erwsh mnhmhs tou ptcb.
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

  // Ksipnaei ta threads pou to perimenoun kai to kanei detached
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

  // ksipname osa threads perimenoun afto pou 8a skotwsoume
  Cond_Broadcast(& CURPTCB->cv);

  /* Kill the thread. */
  kernel_sleep(EXITED, SCHED_USER);
}