#include <stdlib.h>
#include <sys/queue.h>
#include <semaphore.h>
#include "util/so_scheduler.h"

typedef struct {
	unsigned int time_quantum;
	unsigned int io;
	int initialised;
	sem_t semaphore;
} scheduler_t;

typedef struct {
	unsigned int time_remaining;
	unsigned int priority;
	tid_t tid;
	so_handler *handler;
} thread_t;

typedef struct {
    thread_t *thread;
    STAILQ_ENTRY(qentry) qentries;
} qentry;

STAILQ_HEAD(stailhead, qentry);

typedef struct {
	thread_t *thread;
	LIST_ENTRY(lentry) lentries;
} lentry;

LIST_HEAD(listhead, lentry);

/* our instance of scheduler */
static scheduler_t scheduler;

/* the priority queue -> 6 (0 -> 5) queues that will store the threads ordered by priorities */
static struct stailhead *ready;

/* list of events-blocked threads organised by events */
static struct listhead *events;

int so_init(unsigned int time_quantum, unsigned int io) {

	/* exceptions */
	if (time_quantum <= 0 || io > SO_MAX_NUM_EVENTS || scheduler.initialised)
		return -1;

	/* priority queue initialisation */
	ready = (struct stailhead *) malloc((1 + SO_MAX_PRIO) * sizeof(struct stailhead));
	DIE(!ready, "malloc failed");
	for (int i = 0; i <= SO_MAX_PRIO; ++i)
		STAILQ_INIT(&ready[i]);

	/* list of events-blocked threads initialisation */
	events = (struct listhead *) malloc((1 + io) * sizeof(struct listhead));
	DIE(!events, "malloc failed");
	for (unsigned int i = 0; i <= io; ++i)
		LIST_INIT(&events[i]);

	scheduler.initialised = 1;
	scheduler.io = io;
	scheduler.time_quantum = time_quantum;
	int ret = sem_init(&scheduler.semaphore, 0, 1);
	DIE(ret, "semaphore init failed");
	return 0;
}

/*
 * creates a new so_task_t and runs it according to the scheduler
 * + handler function
 * + priority
 * returns: tid of the new task if successful or INVALID_TID
 */
tid_t so_fork(so_handler *func, unsigned int priority) {

	if (!func || priority > SO_MAX_PRIO)
		return INVALID_TID;

	return INVALID_TID;
}

/*
 * waits for an IO device
 * + device index
 * returns: -1 if the device does not exist or 0 on success
 */
int so_wait(unsigned int io) {
	if (io > SO_MAX_NUM_EVENTS)
		return -1;

	return -1;
}

/*
 * signals an IO device
 * + device index
 * return the number of tasks woke or -1 on error
 */
int so_signal(unsigned int io) {
	if (io > SO_MAX_NUM_EVENTS)
		return -1;

	return -1;
}

/*
 * does whatever operation
 */
void so_exec(void) {

}

/*
 * destroys a scheduler
 */
void so_end(void) {

	free(ready);
	free(events);
	scheduler.initialised = 0;
}
