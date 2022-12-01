#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler_struct.h"
#include "constants.h"



int alloc_scheduler_list_of_threads() {
    scheduler->list_of_threads = malloc(MAX * sizeof(queue_thread));
    if (scheduler->list_of_threads == NULL) {
        free(scheduler);
        return EXIT_ERROR;
    }

    return 0;
}

int alloc_real_threads() {
    scheduler->threads = (pthread_t *)malloc(MAX * sizeof(pthread_t));
    if (scheduler->threads == NULL) {
        free(scheduler->list_of_threads);
        free(scheduler);
        return EXIT_ERROR;
    }

    return 0;
}

int alloc_queue() {
    scheduler->queue = malloc(MAX * sizeof(queue_thread));
    if (scheduler->queue == NULL) {   
        free(scheduler->threads);
        free(scheduler->list_of_threads);
        free(scheduler);
        return EXIT_ERROR;
    }

    return 0;
}

int alloc_scheduler(unsigned int time_quantum, unsigned int io) {
    // alloc scheduler
    scheduler = malloc(sizeof(scheduler_struct));
    if (scheduler == NULL) {
        return EXIT_ERROR;
    }

    // initialise scheduler
    scheduler->quantum = time_quantum;
    scheduler->main_thread_id = -1;
    scheduler->io_supported = io;
    scheduler->size = 0;
    scheduler->no_threads = 0;

    int return_code;
    return_code = alloc_scheduler_list_of_threads();
    if (return_code == EXIT_ERROR) {
        return EXIT_ERROR;
    }

    return_code = alloc_real_threads();
    if (return_code == EXIT_ERROR) {
        return EXIT_ERROR;
    }

    return_code = alloc_queue();
    if (return_code == EXIT_ERROR) {
        return EXIT_ERROR;
    }

    return 0;
} 

int alloc_waiting_threads() {
    thread_mat = calloc(LIMIT_IO + 1, sizeof(queue_thread *));
    if (thread_mat == NULL) {
        free(scheduler->list_of_threads);
        free(scheduler->threads);
        free(scheduler->queue);
        free(scheduler);
        return EXIT_ERROR;
    }
    
    for (int i = 0; i < LIMIT_IO; i++) {
        thread_mat[i] = calloc(MAX, sizeof(queue_thread));

        if(thread_mat[i] == NULL) {
            free(thread_mat);
            free(scheduler->list_of_threads);
            free(scheduler->threads);
            free(scheduler->queue);
            free(scheduler);
            return EXIT_ERROR;
        }
    }

    row_size = calloc(LIMIT_IO + 1, sizeof(int));
    if (row_size == NULL) {
        for (int i = 0; i < LIMIT_IO; i++) {
            free(thread_mat[i]);
        }
        free(thread_mat);
        free(scheduler->list_of_threads);
        free(scheduler->threads);
        free(scheduler->queue);
        free(scheduler);
        return EXIT_ERROR;
    }
    return 0;
}

int alloc_semaphores() {
    semaphores = malloc(sizeof(sem_t) * MAX);
    if (semaphores == NULL) {
        free(row_size);
        for (int i = 0; i < LIMIT_IO; i++)
        {
            free(thread_mat[i]);
        }
        free(thread_mat);
        free(scheduler->list_of_threads);
        free(scheduler->threads);
        free(scheduler->queue);
        free(scheduler);
        return EXIT_ERROR;
    }

    sem_init(&sem_term, 0, 1);
    return 0;
}

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{
    // initialise scheduler
    // returns 0 for error, negative for error

    // quantum is null
    if (!time_quantum)
        return EXIT_ERROR;
    // io devices supported surpass limit
    if (io > LIMIT_IO)
        return EXIT_ERROR;

    if (scheduler_state == UNINITIALISED) {
        // alloc the actual scheduler struct
        int return_code = alloc_scheduler(time_quantum, io);
        if (return_code < 0)
            return EXIT_ERROR;
        return_code = alloc_waiting_threads();
        if (return_code < 0)
            return EXIT_ERROR;
        return_code = alloc_semaphores();
        if (return_code < 0)
            return EXIT_ERROR;
        return_code = pthread_mutex_init(&mutex, NULL);
        if (return_code)
            return return_code;

        scheduler_state = INITIALISED;
        return 0;
    }   

    return EXIT_ERROR;
}

DECL_PREFIX int so_wait(unsigned int io)
{
    // current thread blocks in case of event
    // return 0 for existing event else negative value for error

    if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED) {
        return EXIT_ERROR;
    }

    // put thread int the waiting line for the io operation
    thread_mat[io][row_size[io]] = scheduler->main_thread;
    row_size[io]++;
    scheduler->main_thread.status = WAITING_STATE;

    so_exec();

    return 0;
}

DECL_PREFIX  int so_signal(unsigned int io)
{
    // signals threads that wait for an event
    // threads that are signaled enter in the priority queue
    // return number of threads unlocked

    if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED) {
        return EXIT_ERROR;
    }

    // queue all threads for given io => signal those threads
    for(int i = 0; i < row_size[io]; i++) {
        add(thread_mat[io][i]);
        // delete thread from wating state mat by filling that mempory with 0
        memset(&thread_mat[io][i], 0, sizeof(queue_thread));
        thread_mat[io][i].status = READY_STATE;
    }
    
    int result = row_size[io];
    row_size[io] = 0;

    so_exec();

    return result;
}

int change_threads()
{
    // change main thread to first in queue
    scheduler->main_thread = scheduler->queue[0];
    scheduler->main_thread_id = scheduler->queue[0].thread_id;
    scheduler->main_thread.time_quantum = scheduler->quantum;
    scheduler->main_thread.status = RUNNING_STATE;

    // enqueue main thread in place of the first
    delete ();
    add(scheduler->main_thread);

    // execute first thread in queue now
    if (sem_post(&semaphores[scheduler->queue[0].thread_id]))
        return EXIT_ERROR;

    return 0;
}

int schedule_case_one()
{
    // schedule thread when the priority queue is not empty
    int exit = 0;
    // if state is termianted or waiting set new thread
    if (scheduler->main_thread.status == TERMINATED_STATE || scheduler->main_thread.status == WAITING_STATE)
    {
        if (set_main_thread_from_queue())
            return EXIT_ERROR;
        else
            exit = 1;
    }

    if (exit)
        return 0;

    // there is higher priority candidate available
    if (scheduler->queue[0].prio > scheduler->main_thread.prio)
    {
        if (change_threads())
            return EXIT_ERROR;
        else
            exit = 1;
    }

    if (exit)
        return 0;

    // time quantum expired
    if (scheduler->main_thread.time_quantum <= 0)
    {
        // if there is a thread with better or equal priority
        if (scheduler->main_thread.prio <= scheduler->queue[0].prio)
        {
            if (change_threads())
                return EXIT_ERROR;
            else
                exit = 1;

            if (exit)
                return 0;

            // choose the same thread
            // reset its time quantum
            scheduler->main_thread.time_quantum = scheduler->quantum;
            scheduler->main_thread.status = RUNNING_STATE;

            if (sem_post(&semaphores[scheduler->main_thread_id]))
                return EXIT_ERROR;
            else
                exit = 1;

            if (exit)
                return 0;
        }
    }
    else
    {
        // choose the same thread even if its time didnt end
        scheduler->main_thread.status = RUNNING_STATE;

        if (sem_post(&semaphores[scheduler->main_thread_id]))
            return EXIT_ERROR;
        else
            exit = 1;

        if (exit)
            return 0;
    }
    return 0;
}

int schedule_case_two()
{
    // schedule thread when the priority queue is empty
    if (scheduler->main_thread.time_quantum <= 0)
    {
        // choose the same thread
        // reset its time quantum
        scheduler->main_thread.time_quantum = scheduler->quantum;
        scheduler->main_thread.status = RUNNING_STATE;

        if (sem_post(&semaphores[scheduler->main_thread_id]))
            return EXIT_ERROR;
    }
    else
    {
        // choose the same thread even if its time didnt end because there is no better candidate
        scheduler->main_thread.status = RUNNING_STATE;

        if (sem_post(&semaphores[scheduler->main_thread_id]))
            return EXIT_ERROR;
    }

    return 0;
}

int decide_thread() {
    // if current thread is finished and in queue we dont have a candidate
    if (scheduler->main_thread_id != -1) {
        if (scheduler->main_thread.status == TERMINATED_STATE && !scheduler->size)
            return 0;
    } else {
        // case for initial thread
        if (scheduler->size)
            if (set_main_thread_from_queue())
                return EXIT_ERROR;

        if (sem_wait(&sem_term))
            return EXIT_ERROR;
    }

    int err;
    if (scheduler->size) {
        err = schedule_case_one();
        if (err == EXIT_ERROR)
            return EXIT_ERROR;
    }
    else {
        err = schedule_case_two();
        if (err == EXIT_ERROR)
            return EXIT_ERROR;
    }

    return 0;

}

DECL_PREFIX void so_exec()
{
    // decrement time quantum of current thread
    scheduler->main_thread.time_quantum--;
    
    // decide the thread that will take main
    // if decide thread returns non zero value it is error
    if (decide_thread())
        return;

    // if thread changed is the same exit
    if (scheduler->main_thread_id == -1)
        return;

    // if wait sem function returns non zero it is error
    if (sem_wait(&semaphores[scheduler->main_thread_id]))
        return;
}

void *start_routine(void *args)
{
    queue_thread *thread = (queue_thread *)args;

    // wait for schedule
    if (sem_wait(&semaphores[thread->thread_id]))
        return NULL;

    thread->handler(thread->prio);
    // terminate thread
    thread->status = TERMINATED_STATE;
    scheduler->main_thread.status = TERMINATED_STATE;

    // schedule
    if (decide_thread())
        return NULL;

    if (thread->thread_id == 0)
    {
        if (sem_post(&sem_term))
            return NULL;
    }

    return NULL;
}

DECL_PREFIX tid_t so_fork(so_handler *handler, unsigned int priority)
{
    // introduces a new thread
    if (scheduler_state == UNINITIALISED || priority > 5 || handler == NULL)
        return INVALID_TID;

    int last = scheduler->no_threads;

    // initialise the new thread with new data
    scheduler->list_of_threads[last].prio = priority;
    scheduler->list_of_threads[last].handler = handler;

    // initiliase thread with new data; no_threads means last thread in list
    scheduler->list_of_threads[last].thread_id = scheduler->no_threads;
    scheduler->list_of_threads[last].time_quantum = scheduler->quantum;
    scheduler->list_of_threads[last].status = READY_STATE;
    if (sem_init(&semaphores[last], 0, 0))
        return INVALID_TID;

    if (pthread_create(&scheduler->threads[last], NULL, start_routine, &scheduler->list_of_threads[last]))
        return INVALID_TID;

    if (pthread_mutex_lock(&mutex))
        return EXIT_ERROR;

    // enqueue new thread
    add(scheduler->list_of_threads[last]);
    scheduler->no_threads++;

    if (pthread_mutex_unlock(&mutex))
        return EXIT_ERROR;

    so_exec();

    return scheduler->threads[last];
}

DECL_PREFIX void so_end()
{
    // free resources and end scheduler
    if (scheduler_state == UNINITIALISED)
        return;

    // wait for all threads
    if (sem_wait(&sem_term))
        return;

    // join threads
    for (unsigned int i = 0; i < scheduler->no_threads; i++) {
        if (pthread_join(scheduler->threads[i], NULL))
            return;
    }


    // free resources
    for (unsigned int i = 0; i < scheduler->no_threads; i++) { 
        if(sem_destroy(&semaphores[i]))
            return;
    }

    if (sem_destroy(&sem_term))
        return;

    if (pthread_mutex_destroy(&mutex))
        return;

    free(row_size);
    for (int i = 0; i < LIMIT_IO; i++) {
        free(thread_mat[i]);
    }
    free(thread_mat);

    free(scheduler->list_of_threads);
    free(scheduler->threads);
    free(scheduler->queue);
    free(scheduler);

    scheduler_state = UNINITIALISED;
}