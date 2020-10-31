#ifndef __KERNEL_THREADS_H
#define __KERNEL_THREADS_H

/**
  @file kernel_threads
  @brief 


  @{
*/ 
#include "util.h"
#include "tinyos.h"
#include "kernel_sched.h"


typedef struct pointer_thread_control_block{ 
  
  Tid_t tid;              
  PCB* owner_pcb;         /**< Owner's pcb. */
  TCB* thread;

  int exitval;             
  Task task;              /**< The thread's function */
  int argl;               /**< The thread's argument length */
  void* args;             /**< The thread's argument string */

  int ref_count;
  int thread_detached;
  CondVar cv;             /**< Condition variable for @c WaitChild */

  int thread_exited;
  rlnode ptcb_node;  
}PTCB;


#define CURPTCB (CURTHREAD->owner_ptcb)

void start_thread();


#endif