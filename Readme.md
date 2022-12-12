# Thread Scheduler

Preemtive POSIX thread scheduler that uses a round-robin technique.

## Contents

Here you have the file hierarchy:

```
.
├── Makefile
├── Readme.md
├── checker-lin
│   ├── Makefile.checker
│   ├── README
│   ├── _log
│   ├── _test
│   │   ├── output.ref
│   │   ├── run_test.c
│   │   ├── run_test.h
│   │   ├── scheduler_test.h
│   │   ├── so_scheduler.h
│   │   ├── test_exec.c
│   │   ├── test_io.c
│   │   └── test_sched.c
│   └── run_all.sh
├── so_scheduler.c
├── tree.txt
└── util
    └── so_scheduler.h
```

## Usage

This project contains two makefiles:

1. The one in the current directory (in the provided diagram) creates a `libscheduler.so` object and copies it in the /lib directory.

2. The other one runs the checker (and provides an score based on the tests in the _test directory).

## Implementation

The entire scheduler implementation is in `so_scheduler.c` file.
There, the following functions are implemented:

1. `so_init(time_quantum, io)` -> initialises the scheduler
2. `so_fork(func, priority)` -> creates a new thread
3. `so_wait(io)` -> the function that blocks a thread (waiting for an io operation for example): this thread might let another one to execute while waiting
4. `so_signal(io)` -> signals a thread that he may continue his execution (he is no longer blocked by an io operation)
5. `so_exec()` -> generic function for execution -> used merely for testing purposes
6. `so_end()` -> frees the memory and marks the end of the program

### Threads and Scheduler:

For this task, the following structures were used:

1. `thread_t` -> all the informations needed for a thread:

```
typedef struct {
	int time_remaining;
	unsigned int priority;
	tid_t tid;
	so_handler *handler;
	sem_t semaphore;
	int status; /* 0 = ready, 1 = terminated,
				   2 = blocked */
} thread_t;
```

2. `scheduler_t` -> the data regarding the scheduler:

```
typedef struct {
	unsigned int time_quantum;
	unsigned int io;
	int initialised;
	thread_t *running;
	sem_t semaphore;
} scheduler_t;
```

### Data Structures

This scheduler required the following data structures:

1. `ready` -> **priority queue** that stores the threads that are in the **ready** state in order of their static priority. This data structure was implemented as an array of `1 + SO_MAX_PRIO` queues, each one maintaining the threads of the corresponding priority. For the interaction with this priority queue, I have provided the following helpers:

	a. `insert_thread(thread, front)` -> inserts a thread in the corresponding queue. The front parameter represents where in the queue we would like the insertion to occur.

	b. `get_thread()` -> returns the highest priority thread.

	c. `therads_remaining` -> returns 0 if the priority queue is empty and 1 otherwise.

2. `events` -> an **array of lists** that stores, for each list, the threads blocked by the event (io).

3. `ended` -> **list** of terminated threads, ready to be freed

### Flow

The scheduler does the following:

1. First of all, the scheduler is initialised.

2. Secondly, a so_fork triggers the apparition of a thread. Using semaphores, we handle the order of execution: at each moment, there is only one thread that can execute (due to the `sem_wait` in the `thread_func` routine). After each instruction (`so_fork`, `so_wait`, `so_exec`, `so_signal`) we try to set the running thread to be the one with the highest priority. Also, since we use a round-robin technique, we let a thread to execute only for a limited number of instructions (then, we choose another equal priority thread -> this action is taken in order to **increase fairness** and to **decrease waiting time**).

3. Finally, the scheduler is freed.

## Github

You can find the project on [Github](https://github.com/VladNegoita/ThreadScheduler).

## References
* Linux Manual Page: [stailq](https://man7.org/linux/man-pages/man3/stailq.3.html), [semaphores](https://man7.org/linux/man-pages/man7/sem_overview.7.html), [pthreads](https://man7.org/linux/man-pages/man7/pthreads.7.html).

###### Copyright Vlad Negoita 321CA vlad1negoita@gmail.com