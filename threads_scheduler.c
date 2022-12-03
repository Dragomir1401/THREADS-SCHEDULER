// Copyright 2022 Dragomir Andrei
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "scheduler_struct.h"

// recursive function to free memory based on needs
void free_resources(int case_id)
{
	switch (case_id) {
	case 0:
		free(row_size);
		free_resources(1);

		break;
	case 1:
		for (int i = 0; i < LIMIT_IO; i++)
			free(thread_mat[i]);
		free_resources(2);

		break;
	case 2:
		free(thread_mat);
		free_resources(3);

		break;
	case 3:
		free(scheduler->queue);
		free_resources(4);

		break;
	case 4:
		free(scheduler->threads);
		free_resources(5);

		break;
	case 5:
		free(scheduler->list_of_threads);
		free_resources(6);

		break;
	case 6:
		free(scheduler);

		break;

	default:
		break;
	}

}

// add thread in queue based on priority and shift queue
void queue_add(queue_thread thread)
{
	// find the place in queue for the new thread
	int index = 0, i;

	i = 0;
	while (i < scheduler->size) {
		if (scheduler->queue[i].prio >= thread.prio)
			i++;
		else
			break;
	}
	index = i;

	// shift the other threads
	memmove(&scheduler->queue[index + 1], &scheduler->queue[index], (scheduler->size - index) * sizeof(queue_thread));

	// place the new thread
	scheduler->size++;
	scheduler->queue[index] = thread;
}

// remove element from queue
void queue_remove(void)
{
	// deletes first element from queue
	memmove(&scheduler->queue[0], &scheduler->queue[1], scheduler->size * sizeof(queue_thread));

	// decrement size
	scheduler->size--;
}

// pop thread from queue and set it running
int set_main_thread_from_queue(void)
{
	queue_thread new_thread = scheduler->queue[0];

	scheduler->main_thread = new_thread;
	scheduler->main_thread_id = new_thread.thread_id;
	scheduler->main_thread.time_quantum = scheduler->quantum;
	scheduler->main_thread.status = RUNNING;

	queue_remove();

	// execute thread
	// value non zero is error
	int error = sem_post(&semaphores[scheduler->main_thread_id]);

	if (error)
		return error;

	return 0;
}

// alloc list of logic threads
int alloc_scheduler_list_of_threads()
{
	scheduler->list_of_threads = malloc(MAX * sizeof(queue_thread));
	if (scheduler->list_of_threads == NULL) {
		free_resources(6);
		return EXIT_ERROR;
	}

	return 0;
}

// alloc list of real cpu threads
int alloc_real_threads(void)
{
	scheduler->threads = (pthread_t *)malloc(MAX * sizeof(pthread_t));
	if (scheduler->threads == NULL) {
		free_resources(5);
		return EXIT_ERROR;
	}

	return 0;
}

// alloc priority queue
int alloc_queue(void)
{
	scheduler->queue = malloc(MAX * sizeof(queue_thread));
	if (scheduler->queue == NULL) {
		free_resources(4);
		return EXIT_ERROR;
	}

	return 0;
}

// alloc actual scheduler
int alloc_scheduler(unsigned int time_quantum, unsigned int io)
{
	// alloc scheduler
	scheduler = malloc(sizeof(scheduler_struct));
	if (scheduler == NULL)
		return EXIT_ERROR;

	// initialise scheduler
	scheduler->quantum = time_quantum;
	scheduler->main_thread_id = -1;
	scheduler->io_supported = io;
	scheduler->size = 0;
	scheduler->no_threads = 0;

	int return_code;

	return_code = alloc_scheduler_list_of_threads();
	if (return_code == EXIT_ERROR)
		return EXIT_ERROR;

	return_code = alloc_real_threads();
	if (return_code == EXIT_ERROR)
		return EXIT_ERROR;

	return_code = alloc_queue();
	if (return_code == EXIT_ERROR)
		return EXIT_ERROR;

	return 0;
}

// alloc waiting threads matrix
int alloc_thread_mat(void)
{
	thread_mat = calloc(LIMIT_IO + 1, sizeof(queue_thread *));
	if (thread_mat == NULL) {
		free_resources(3);
		return EXIT_ERROR;
	}

	for (int i = 0; i < LIMIT_IO; i++) {
		thread_mat[i] = calloc(MAX, sizeof(queue_thread));

		if (thread_mat[i] == NULL) {
			free_resources(2);
			return EXIT_ERROR;
		}
	}

	row_size = calloc(LIMIT_IO + 1, sizeof(int));
	if (row_size == NULL) {
		free_resources(1);
		return EXIT_ERROR;
	}

	return 0;
}

// alloc semaphore arraay
int alloc_semaphores(void)
{
	semaphores = malloc(sizeof(sem_t) * MAX);
	if (semaphores == NULL) {
		free_resources(0);
		return EXIT_ERROR;
	}

	sem_init(&sem_term, 0, 1);

	return 0;
}

// initialise scheduler and other structures
int so_init(unsigned int time_quantum, unsigned int io)
{
	// initialise scheduler
	// returns 0 for success, negative for error

	// quantum is null
	if (!time_quantum)
		return EXIT_ERROR;

	// io devices supported surpass limit
	if (io > LIMIT_IO)
		return EXIT_ERROR;

	if (scheduler_state == UNINITIALISED) {
		// alloc the actual scheduler struct
		int return_code = alloc_scheduler(time_quantum, io);

		if (return_code == EXIT_ERROR)
			return EXIT_ERROR;

		return_code = alloc_thread_mat();
		if (return_code == EXIT_ERROR)
			return EXIT_ERROR;

		return_code = alloc_semaphores();
		if (return_code == EXIT_ERROR)
			return EXIT_ERROR;

		scheduler_state = INITIALISED;

		return 0;
	}

	return EXIT_ERROR;
}

// makes thread wait on io
int so_wait(unsigned int io)
{
	// current thread blocks in case of event
	// return 0 for existing event else negative value for error

	if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED)
		return EXIT_ERROR;

	// put thread int the waiting line for the io operation
	thread_mat[io][row_size[io]] = scheduler->main_thread;
	scheduler->main_thread.status = WAITING;
	row_size[io]++;

	so_exec();

	return 0;
}

// wakes up a row of waiting threads
void signal_row(unsigned int io)
{
	int cnt = 0;

	while (cnt < row_size[io]) {
		// extract waiting thread from matrix
		queue_thread thread = thread_mat[io][cnt];

		// set status for signaled thread
		thread.status = READY;

		// add the new ready thread in thread queue
		queue_add(thread);

		// delete thread from wating status mat by filling that memory with 0
		memset(&thread_mat[io][cnt], 0, sizeof(queue_thread));

		cnt++;
	}
}

// signals threads waiting for a specific io
int so_signal(unsigned int io)
{
	// signals threads that wait for an event
	// threads that are signaled enter in the prio queue
	// return number of threads unlocked

	if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED)
		return EXIT_ERROR;

	// queue all threads for given io => signal those threads
	signal_row(io);

	int result = row_size[io];

	row_size[io] = 0;

	so_exec();

	return result;
}

// changes main thread
void assign_main_thread(queue_thread thread, int thread_id, int quantum)
{
	scheduler->main_thread.status = RUNNING;
	scheduler->main_thread.time_quantum = quantum;
	scheduler->main_thread_id = thread_id;
	scheduler->main_thread = thread;
}

// switches main thread with first in queue
int change_threads(void)
{

	queue_thread main = scheduler->main_thread;

	main.time_quantum = scheduler->quantum;

	assign_main_thread(scheduler->queue[0], scheduler->queue[0].thread_id, scheduler->quantum);

	// save current first in queue id
	int first_in_queue_id = scheduler->queue[0].thread_id;

	// enqueue main thread in place of the first
	queue_remove();
	queue_add(main);

	// execute first thread in queue now
	if (sem_post(&semaphores[first_in_queue_id]))
		return EXIT_ERROR;

	return 0;
}

// schedules when there are candidates in the priority queue
int schedule_case_one(void)
{
	// schedule thread when the prio queue is not empty

	// if status is termianted or waiting set new thread
	if (scheduler->main_thread.status == TERMINATED || scheduler->main_thread.status == WAITING) {
		if (set_main_thread_from_queue())
			return EXIT_ERROR;
		return 0;
	}


	// there is higher prio candidate available
	if (scheduler->queue[0].prio > scheduler->main_thread.prio) {
		if (change_threads())
			return EXIT_ERROR;
		return 0;
	}


	// time quantum expired
	if (scheduler->main_thread.time_quantum <= 0) {
		// if there is a thread with equal prio
		if (scheduler->main_thread.prio == scheduler->queue[0].prio) {
			if (change_threads())
				return EXIT_ERROR;

		} else {
			// choose the same thread
			// reset its time quantum
			scheduler->main_thread.time_quantum = scheduler->quantum;
			scheduler->main_thread.status = RUNNING;

			if (sem_post(&semaphores[scheduler->main_thread_id]))
				return EXIT_ERROR;
		}

		return 0;
	}

	// choose the same thread even if its time didnt end
	scheduler->main_thread.status = RUNNING;
	if (sem_post(&semaphores[scheduler->main_thread_id]))
		return EXIT_ERROR;

	return 0;
}

// schedules when there are no candidates in the queue
int schedule_case_two(void)
{
	// schedule thread when the prio queue is empty
	if (scheduler->main_thread.time_quantum <= 0) {
		// choose the same thread
		// reset its time quantum
		scheduler->main_thread.time_quantum = scheduler->quantum;
		scheduler->main_thread.status = RUNNING;

		if (sem_post(&semaphores[scheduler->main_thread_id]))
			return EXIT_ERROR;
	} else {
		// choose the same thread even if its time didnt end because there is no better candidate
		scheduler->main_thread.status = RUNNING;

		if (sem_post(&semaphores[scheduler->main_thread_id]))
			return EXIT_ERROR;
	}

	return 0;
}

// selects thread in action
int decide_thread(void)
{
	// if current thread is finished and in queue we dont have a candidate
	if (scheduler->main_thread_id != -1)
		if (scheduler->main_thread.status == TERMINATED && !scheduler->size)
			return 0;

	if (scheduler->main_thread_id == -1) {
		// case for initial thread
		if (scheduler->size)
			if (set_main_thread_from_queue())
				return EXIT_ERROR;

		if (sem_wait(&sem_term))
			return EXIT_ERROR;

		return 0;
	}

	int err;
	if (scheduler->main_thread_id != -1) {
		if (scheduler->size) {
			// case for non empty priority queue
			err = schedule_case_one();
			if (err < 0)
				return EXIT_ERROR;
			else
				return 0;
		} else {
			// case for empty priority queue
			err = schedule_case_two();
			if (err < 0)
				return EXIT_ERROR;
			else
				return 0;
		}
	}

	return 0;
}

// simulates execution of an instruction
void so_exec(void)
{

	// decrement time quantum of current thread
	scheduler->main_thread.time_quantum--;

	// decide the thread that will take main
	// if decide thread returns non zero value it is error

	queue_thread save_thread = scheduler->main_thread;

	int id = scheduler->main_thread_id;

	if (decide_thread())
		return;

	// if thread changed is the same exit
	if (id == -1)
		return;

	// if wait sem function returns non zero it is error
	if (sem_wait(&semaphores[save_thread.thread_id]))
		return;
}

// start routine for pthread_create
void *start_thread(void *args)
{
	queue_thread *thread = (queue_thread *)args;

	// wait for schedule
	if (sem_wait(&semaphores[thread->thread_id]))
		return NULL;

	thread->handler(thread->prio);
	// terminate thread
	thread->status = TERMINATED;
	scheduler->main_thread.status = TERMINATED;

	// schedule
	if (decide_thread())
		return NULL;

	if (thread->thread_id == 0)
		if (sem_post(&sem_term))
			return NULL;

	return NULL;
}

// starts and introduces in scheduling a new thread
tid_t so_fork(so_handler *handler, unsigned int prio)
{
	// introduces a new thread
	if (scheduler_state == UNINITIALISED || prio > 5 || handler == NULL)
		return INVALID_TID;

	int last = scheduler->no_threads;

	// initialise the new thread with new data
	scheduler->list_of_threads[last].prio = prio;
	scheduler->list_of_threads[last].handler = handler;

	// initiliase thread with new data; no_threads means last thread in list
	scheduler->list_of_threads[last].thread_id = scheduler->no_threads;
	scheduler->list_of_threads[last].time_quantum = scheduler->quantum;
	scheduler->list_of_threads[last].status = READY;

	if (sem_init(&semaphores[last], 0, 0))
		return INVALID_TID;

	if (pthread_create(&scheduler->threads[last], NULL, start_thread, &scheduler->list_of_threads[last]))
		return INVALID_TID;

	// enqueue new thread
	queue_add(scheduler->list_of_threads[last]);
	scheduler->no_threads++;

	so_exec();

	return scheduler->threads[last];
}

// frees all memory and waits all threads to finish
void so_end(void)
{
	// free resources and end scheduler
	if (scheduler_state == UNINITIALISED)
		return;

	// wait for all threads
	if (sem_wait(&sem_term))
		return;

	// join threads
	for (unsigned int i = 0; i < scheduler->no_threads; i++)
		if (pthread_join(scheduler->threads[i], NULL))
			return;

	// free resources
	for (unsigned int i = 0; i < scheduler->no_threads; i++)
		if (sem_destroy(&semaphores[i]))
			return;

	if (sem_destroy(&sem_term))
		return;

	free_resources(0);
	free(semaphores);

	// set state to uninitialised
	scheduler_state = UNINITIALISED;
}
