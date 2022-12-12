#include <stdlib.h>
#include <sys/queue.h>
#include <semaphore.h>
#include <pthread.h>
#include "utils.h"
#include "util/so_scheduler.h"

typedef struct {
	int time_remaining;
	unsigned int priority;
	tid_t tid;
	so_handler *handler;
	sem_t semaphore;
	int status;
} thread_t;

typedef struct {
	unsigned int time_quantum;
	unsigned int io;
	int initialised;
	thread_t *running;
	sem_t semaphore;
} scheduler_t;

struct qentry{
    thread_t *thread;
    STAILQ_ENTRY(qentry) qentries;
};

STAILQ_HEAD(stailhead, qentry);

/* helpers */
void insert_thread(thread_t *thread, int front);
void *thread_func(void *args);
thread_t *get_thread(void);
void choose_thread(void);
int threads_remaining();

/* our instance of scheduler */
static scheduler_t scheduler;

/* the priority queue -> 6 (0 -> 5) queues that will store the threads ordered by priorities */
static struct stailhead ready[1 + SO_MAX_PRIO];

/* list of terminated threads */
static struct stailhead ended;

/* list of events-blocked threads organised by events */
static struct stailhead *events;

int so_init(unsigned int time_quantum, unsigned int io) {

	/* exceptions */
	if (time_quantum <= 0 || io > SO_MAX_NUM_EVENTS || scheduler.initialised)
		return -1;

	/* priority queue initialisation */
	for (int i = 0; i <= SO_MAX_PRIO; ++i)
		STAILQ_INIT(&ready[i]);

	/* list of events-blocked threads initialisation */
	events = (struct stailhead *) malloc(io * sizeof(struct stailhead));
	DIE(!events, "malloc failed");
	for (unsigned int i = 0; i < io; ++i)
		STAILQ_INIT(&events[i]);

	/* list of terminated threads */
	STAILQ_INIT(&ended);

	/* scheduler initialisation */
	scheduler.initialised = 1;
	scheduler.io = io;
	scheduler.time_quantum = time_quantum;
	scheduler.running = NULL;

	/* scheduler semaphore initialisation */
	int err = sem_init(&scheduler.semaphore, 0, 1);
	DIE(err, "semaphore init failed");
	return 0;
}

tid_t so_fork(so_handler *func, unsigned int priority) {

	/* exceptions */
	if (!func || priority > SO_MAX_PRIO)
		return INVALID_TID;

	/* thread initialisation */
	thread_t *thread;
	thread = (thread_t *) malloc(sizeof(thread_t));
	DIE(!thread, "malloc failed");

	thread->handler = func;
	thread->priority = priority;
	thread->status = 0;
	thread->tid = INVALID_TID;
	thread->time_remaining = scheduler.time_quantum;

	/* thread semaphore initialisation */
	int err = sem_init(&thread->semaphore, 0, 0);
	DIE(err, "semaphore init failed");

	/* thread creation */
	err = pthread_create(&thread->tid, NULL, thread_func, (void *)thread);
	DIE(err, "pthread create failed");

	insert_thread(thread, 0);

	if (scheduler.running)
		--scheduler.running->time_remaining;

	choose_thread();

	return thread->tid;
}

void so_end(void) {

	if (!scheduler.initialised)
		return;

	/* give time to other threads for finishing their execution */
	int err = sem_wait(&scheduler.semaphore);
	DIE(err, "semaphore wait failed");

	/* add the last running thread into the terminated list */
	struct qentry *node;
	if (scheduler.running) {
		node = (struct qentry *) malloc(sizeof(struct qentry));
		DIE(!node, "malloc failed");
		node->thread = scheduler.running; 
		STAILQ_INSERT_HEAD(&ended, node, qentries);
	}

	scheduler.running = NULL;

	/* joining threads */
	STAILQ_FOREACH(node, &ended, qentries) {
		err = pthread_join(node->thread->tid, NULL);
		DIE(err, "thread join failed");
	}

	/*destroying semaphors and freeing memory necessary for threads */
	STAILQ_FOREACH(node, &ended, qentries) {
		err = sem_destroy(&node->thread->semaphore);
		DIE(err, "sem_destroy failed");
		free(node->thread);
	}

	/* freeing the nodes for the lists */
	struct qentry *n1 = STAILQ_FIRST(&ended), *n2;
	while (n1 != NULL) {
		n2 = STAILQ_NEXT(n1, qentries);
		free(n1);
		n1 = n2;
	}

	free(events);
	scheduler.initialised = 0;
	err = sem_destroy(&scheduler.semaphore);
	DIE(err, "sem_destroy failed");
}

/* inserts a thread in the priority queue */
void insert_thread(thread_t *thread, int front) {
	struct qentry *node = (struct qentry *) malloc(sizeof(struct qentry));
	DIE(!node, "malloc failed");

	node->thread = thread;
	/* this function offers the posibility to insert
	at the begining of the queue as well as at the end */
	if (front)
		STAILQ_INSERT_HEAD(&ready[thread->priority], node, qentries);
	else
		STAILQ_INSERT_TAIL(&ready[thread->priority], node, qentries);
}

/* the function a thread will execute (thread routine) */
void *thread_func(void *args) {

	thread_t *thread = (thread_t *)args;

	int err = sem_wait(&thread->semaphore);
	DIE(err, "sem_wait failed");

	thread->handler(thread->priority);
	thread->status = 1;

	choose_thread();

	return NULL;
}

/* gets the highest priority ready thread from the priority_queue */
thread_t *get_thread(void) {

	struct qentry *node = NULL;
	for (int priority = SO_MAX_PRIO; priority >= 0; --priority) {
		if (!STAILQ_EMPTY(&ready[priority])) {
			node = STAILQ_FIRST(&ready[priority]);
			STAILQ_REMOVE_HEAD(&ready[priority], qentries);
			break;
		}
	}

	if (!node)
		return NULL;

	thread_t *thread = node->thread;
	free(node);

	return thread;
}

void choose_thread(void) {

	/* we may now clear everything */
	if (!threads_remaining() && (!scheduler.running || scheduler.running->status == 1)) {
		int err = sem_post(&scheduler.semaphore);
		DIE(err, "sem_post failed");
		return;
	}

	thread_t *thread = get_thread(), *aux = scheduler.running;

	/* the only remaining one is the running one */
	if (!thread) {
		if (scheduler.running->time_remaining <= 0)
			scheduler.running->time_remaining = scheduler.time_quantum;
		return;
	}

	/* first thread */
	if (scheduler.running == NULL) {
		int err = sem_wait(&scheduler.semaphore);
		DIE(err, "sem_wait failed");
		scheduler.running = thread;
		thread->time_remaining = scheduler.time_quantum;
		err = sem_post(&thread->semaphore);
		DIE(err, "sem_post failed");
		return;
	}

	/* terminated thread */
	if (scheduler.running->status == 1) {

		struct qentry *node = (struct qentry *) malloc(sizeof(struct qentry));
		DIE(!node, "malloc failed");

		node->thread = scheduler.running;
		STAILQ_INSERT_HEAD(&ended, node, qentries);

		thread->time_remaining = scheduler.time_quantum;
		scheduler.running = thread;
		int err = sem_post(&thread->semaphore);
		DIE(err, "sem_post failed");
		return;
	}

	/* blocked (waiting) thread */
	if (scheduler.running->status == 2) {
		thread->time_remaining = scheduler.time_quantum;
		scheduler.running = thread;
		int err = sem_post(&thread->semaphore);
		DIE(err, "sem_post failed");
		err = sem_wait(&aux->semaphore);
		DIE(err, "sem_wait failed");
		return;
	}

	printf("aici a zis Matt\n");

	/* a greater priority thread appeared */
	if (scheduler.running->priority < thread->priority) {
		insert_thread(scheduler.running, 0);
		thread->time_remaining = scheduler.time_quantum;
		scheduler.running = thread;
		int err = sem_post(&thread->semaphore);
		DIE(err, "sem_post failed");
		err = sem_wait(&aux->semaphore);
		DIE(err, "sem_wait failed");
		return;
	}

	/* change the thread after time_quant if another thread
	of equal priority exists, maintain equity */
	if (scheduler.running->time_remaining <= 0) {
		if (scheduler.running->priority == thread->priority) {
			insert_thread(scheduler.running, 0);
			thread->time_remaining = scheduler.time_quantum;
			scheduler.running = thread;
			int err = sem_post(&thread->semaphore);
			DIE(err, "sem_post failed");
			err = sem_wait(&aux->semaphore);
			DIE(err, "sem_wait failed");
		} else {
			insert_thread(thread, 1);
			scheduler.running->time_remaining = scheduler.time_quantum;
		}
		return;
	}

	insert_thread(thread, 1);
}

/* checks whether there are reamining threads in the ready state */
int threads_remaining() {

	for (int priority = 0; priority <= SO_MAX_PRIO; ++priority)
		if (!STAILQ_EMPTY(&ready[priority]))
			return 1;

	return 0;
}

int so_wait(unsigned int io) {

	/* exceptions */
	if (io >= scheduler.io)
		return -1;

	/* blocked (waiting) status */
	scheduler.running->status = 2;
	scheduler.running->time_remaining--;

	struct qentry *node = (struct qentry *) malloc(sizeof(struct qentry));
	DIE(!node, "malloc failed");
	node->thread = scheduler.running;

	STAILQ_INSERT_HEAD(&events[io], node, qentries);

	choose_thread();

	return 0;
}

int so_signal(unsigned int io) {

	/* exceptions */
	if (io >= scheduler.io)
		return -1;

	struct qentry *node = NULL;

	/* ready state */
	int count_signaled_threads = 0;
	while (!STAILQ_EMPTY(&events[io])) {
		node = STAILQ_FIRST(&events[io]);
		node->thread->status = 0;
		insert_thread(node->thread, 0);
		STAILQ_REMOVE_HEAD(&events[io], qentries);
		free(node);
		++count_signaled_threads;
	}

	scheduler.running->time_remaining--;
	choose_thread();

	return count_signaled_threads;
}

void so_exec(void) {
	if (scheduler.running)
		scheduler.running->time_remaining--;

	choose_thread();
}
