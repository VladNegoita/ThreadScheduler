#include <stdlib.h>
#include <sys/queue.h>
#include <semaphore.h>
#include <pthread.h>
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

struct lentry {
	thread_t *thread;
	LIST_ENTRY(lentry) lentries;
};

LIST_HEAD(listhead, lentry);

void insert_thread(thread_t *thread, int front);
void *thread_func(void *args);
thread_t *get_thread(void);
void choose_thread(void);
int threads_remaining();

/* our instance of scheduler */
static scheduler_t scheduler;

/* the priority queue -> 6 (0 -> 5) queues that will store the threads ordered by priorities */
static struct stailhead *ready;
static struct stailhead ended;

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

	STAILQ_INIT(&ended);

	/* scheduler initialisation */
	scheduler.initialised = 1;
	scheduler.io = io;
	scheduler.time_quantum = time_quantum;
	scheduler.running = NULL;

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

	int err = sem_init(&thread->semaphore, 0, 0);
	DIE(err, "semaphore init failed");

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

	sem_wait(&scheduler.semaphore);

	struct qentry *node;
	if (scheduler.running) {
		node = (struct qentry *) malloc(sizeof(struct qentry));
		node->thread = scheduler.running; 
		STAILQ_INSERT_HEAD(&ended, node, qentries);
	}

	scheduler.running = NULL;

	STAILQ_FOREACH(node, &ended, qentries) {
		int err = pthread_join(node->thread->tid, NULL);
		DIE(err, "thread join failed");
	}

	STAILQ_FOREACH(node, &ended, qentries) {
		sem_destroy(&node->thread->semaphore);
		free(node->thread);
	}

	struct qentry *n1 = STAILQ_FIRST(&ended), *n2;
	while (n1 != NULL) {
		n2 = STAILQ_NEXT(n1, qentries);
		free(n1);
		n1 = n2;
	}

	free(ready);
	free(events);
	scheduler.initialised = 0;
	sem_destroy(&scheduler.semaphore);
}

void insert_thread(thread_t *thread, int front) {
	struct qentry *node = (struct qentry *) malloc(sizeof(struct qentry));
	DIE(!node, "malloc failed");

	node->thread = thread;
	if (front)
		STAILQ_INSERT_HEAD(&ready[thread->priority], node, qentries);
	else
		STAILQ_INSERT_TAIL(&ready[thread->priority], node, qentries);
}

void *thread_func(void *args) {

	thread_t *thread = (thread_t *)args;

	int err = sem_wait(&thread->semaphore);
	DIE(err, "sem_wait failed");

	thread->handler(thread->priority);
	thread->status = 1;

	choose_thread();

	return NULL;
}

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

	if (!threads_remaining() && (!scheduler.running || scheduler.running->status == 1)) {
		sem_post(&scheduler.semaphore);
		return;
	}

	thread_t *thread = get_thread(), *aux = scheduler.running;

	if (!thread) {
		if (scheduler.running->time_remaining <= 0)
			scheduler.running->time_remaining = scheduler.time_quantum;
		return;
	}

	if (scheduler.running == NULL) {
		sem_wait(&scheduler.semaphore);
		printf("first time");
		scheduler.running = thread;
		thread->time_remaining = scheduler.time_quantum;
		sem_post(&thread->semaphore);
		return;
	}

	if (scheduler.running->status == 1) {

		struct qentry *node = (struct qentry *) malloc(sizeof(struct qentry));
		DIE(!node, "malloc failed");

		node->thread = scheduler.running;
		STAILQ_INSERT_HEAD(&ended, node, qentries);

		thread->time_remaining = scheduler.time_quantum;
		scheduler.running = thread;
		sem_post(&thread->semaphore);
		return;
	}

	if (scheduler.running->priority < thread->priority) {
		insert_thread(scheduler.running, 0);
		thread->time_remaining = scheduler.time_quantum;
		scheduler.running = thread;
		sem_post(&thread->semaphore);
		sem_wait(&aux->semaphore);
		return;
	}

	if (scheduler.running->time_remaining <= 0) {
		printf("fara timp\n");
		if (scheduler.running->priority == thread->priority) {
			insert_thread(scheduler.running, 0);
			thread->time_remaining = scheduler.time_quantum;
			scheduler.running = thread;
			sem_post(&thread->semaphore);
			sem_wait(&aux->semaphore);
		} else {
			insert_thread(thread, 1);
			scheduler.running->time_remaining = scheduler.time_quantum;
		}
		return;
	}

	insert_thread(thread, 1);
}

int threads_remaining() {

	for (int priority = 0; priority <= SO_MAX_PRIO; ++priority)
		if (!STAILQ_EMPTY(&ready[priority]))
			return 1;

	return 0;
}

/*
 * waits for an IO device
 * + device index
 * returns: -1 if the device does not exist or 0 on success
 */
int so_wait(unsigned int io) {

	/* exceptions */
	if (io > scheduler.io)
		return -1;

	return -1;
}

/*
 * signals an IO device
 * + device index
 * return the number of tasks woke or -1 on error
 */
int so_signal(unsigned int io) {

	/* exceptions */
	if (io > SO_MAX_NUM_EVENTS)
		return -1;

	return -1;
}

void so_exec(void) {
	if (scheduler.running)
		scheduler.running->time_remaining--;

	printf("%d\n", scheduler.running->time_remaining);
	choose_thread();
}
