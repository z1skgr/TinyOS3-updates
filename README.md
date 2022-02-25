
# TinyOS v.3

TinyOS is a very small operating system, built on top of a simple-minded virtual machine, whose purpose is
purely educational. It is not related in any way to the well-known operating system for wireless sensors,
but since it was first conceived in 2003, there was a name collision that I have not yet resolved.
This code (in its long history) has been used for many years to teach the Operating Systems course
at the Technical University of Crete.

In its current incarnation, tinyos supports a multicore preemptive scheduler, serial terminal devices, and a
unix like process model. It does not support (yet) memory management, block devices, or network devices. These
extensions are planned for the future.

## Quick start

```
$ git clone https://github.com/vsamtuc/tinyos3.git
$ cd tinyos3
$ touch .depend
```
After downloading the code, just build it.
```
$ make
```
and try to discover system and test files. 



If all goes well, the code should build without warnings. 

### Test

Some terminal tests for Linux Ubuntu
```
$ gnome-terminal -e "./terminal 0"
$ gnome-terminal -e "./terminal 1"
$ ./test_bios2
$ ./bios_example5
```

You can run your first instance of tinyos,
a simulation of Dijkstra's Dining Philosophers.
```
$ ./mtask 1 0 5 5
FMIN = 27    FMAX = 37
*** Booting TinyOS
[T] .  .  .  .      0 has arrived
[E] .  .  .  .      0 is eating
[T] .  .  .  .      0 is thinking
[E] .  .  .  .      0 is eating
 E [T] .  .  .      1 has arrived
 E [H] .  .  .      1 waits hungry
 E  H [T] .  .      2 has arrived
< more lines deleted >
```

Then, you are ready to start reading the documentation (you will need `doxygen` to build it)
```
make doc
```
Point your browser at file  `doc/html/index.html`.  Happy reading!


### Build dependencies

Tinyos is developed, and will probably only run on Linux (its bios.c file uses Linux-specific system 
calls, in particular signal streams). Any recent (last few years) version of Linux should be sufficient.

Working with the code, at the basic level, requires a recent GCC compiler (with support for C11). The
standard packages `doxygen` and `valgrind` with their dependencies (e.g., `graphviz`) are also needed 
for anything serious, as well as the GDB debugger.

### Upgrades 
* Multilevel Feedback Queues (changes to initial  kernel_sched.c)
    * Αrray of queues, a queue for each priority.
    * Priotity => TCB of an integer field
    * Quantum ends => Scheduler calculates a new value for that field, which is actually
the queue to which the thread will be added.
    * First element of  first non-empty queue is the next thread.
* Multithreading processess (syscall changes to initial kernel_thread.c)
    * PTCB <> TCB 
    * CreateThread
    * ThreadSelf
    * ThreadJoin
    * ThreadExit
    * TreadDetach

* Process communication mechanisms, pipes and sockets. (syscall changes to initial kernel_socket, kernel_pipe.c, kernel_steams.c)
   *  Socket
   *  Connect
   *  Listen
   *  Accept
   *  Shutdown

* User program for standard output information about the system (syscall changes to initial kernel_proc).

For more informations [^1][^2], check doc folder.


### Acknowledgements
* This project was implemented for the requirements of the lesson Operating Systems
* Many credits to  professor [Vsam](https://github.com/vsamtuc)


[^1]: Folder 1 has  Feedback Queues and Threads
[^2]: Folder 2 with all upgrades.

