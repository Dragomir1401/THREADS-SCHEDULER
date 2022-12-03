// Copyright 2022 Dragomir Andrei

#include <pthread.h>
#include <semaphore.h>
#include "so_scheduler.h"
#include "constants.h"


typedef struct queue_thread {
	// priority of the thread
	unsigned int prio;

	// id of the thread
	int thread_id;

	// time spent on cpu
	int time_quantum;

	// current status
	int status;

	// handler function
	so_handler *handler;
} queue_thread;

typedef struct scheduler_struct {
	// threads queue
	queue_thread *queue;

	// priority queue size
	int size;

	// actual list of threads
	queue_thread *list_of_threads;

	// thread currently on cpu
	queue_thread main_thread;

	// current thread id
	int main_thread_id;

	// real treads
	pthread_t *threads;

	// io devices supported
	unsigned int io_supported;

	// number of total threads
	unsigned int no_threads;

	// time quantum
	unsigned int quantum;

} scheduler_struct;

// actual scheduler
static scheduler_struct *scheduler;

// matrix to store threads waiting on every operation
static queue_thread **thread_mat;
// size of rows in the waiting threads mat
static int *row_size;

// array of semaphores
static sem_t *semaphores;
// tracks if all threads finished execution
static sem_t sem_term;

// status of scheduler - initialised or not
static unsigned int scheduler_state;
