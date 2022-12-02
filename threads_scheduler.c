#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "scheduler_struct.h"

void free_resources() {
    for (int i = 0; i < LIMIT_IO; i++) {
        free(thread_mat[i]);
    }
    free(thread_mat);
    free(row_size);

    free(scheduler->list_of_threads);
    free(scheduler->threads);
    free(scheduler->queue);
    free(scheduler);
}

void queue_add(queue_thread thread) {
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

void queue_remove() {
    // deletes first element from queue
    memmove(&scheduler->queue[0], &scheduler->queue[1], scheduler->size * sizeof(queue_thread));

    // decrement size
    scheduler->size--;
}

int set_main_thread_from_queue() {
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

int alloc_thread_mat() {
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
        free_resources();
        return EXIT_ERROR;
    }

    sem_init(&sem_term, 0, 1);
    return 0;
}

int so_init(unsigned int time_quantum, unsigned int io)
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
        if (return_code == EXIT_ERROR)
            return EXIT_ERROR;
        return_code = alloc_thread_mat();
        if (return_code == EXIT_ERROR)
            return EXIT_ERROR;
        return_code = alloc_semaphores();
        if (return_code == EXIT_ERROR)
            return EXIT_ERROR;
        return_code = pthread_mutex_init(&mutex, NULL);
        if (return_code)
            return EXIT_ERROR;

        scheduler_state = INITIALISED;
        return 0;
    }   

    return EXIT_ERROR;
}

int so_wait(unsigned int io)
{
    // current thread blocks in case of event
    // return 0 for existing event else negative value for error

    if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED) {
        return EXIT_ERROR;
    }

    // put thread int the waiting line for the io operation
    thread_mat[io][row_size[io]] = scheduler->main_thread;
    scheduler->main_thread.status = WAITING;
    row_size[io]++;

    so_exec();

    return 0;
}

void signal_row(unsigned int io) {
    int cnt = 0;
    while (cnt < row_size[io]) {
        // set status for signaled thread
        thread_mat[io][cnt].status = READY;

        // add the new ready thread in thread queue
        queue_add(thread_mat[io][cnt]);

        // delete thread from wating status mat by filling that memory with 0
        memset(&thread_mat[io][cnt], 0, sizeof(queue_thread));

        cnt++;
    }
}

int so_signal(unsigned int io)
{
    // signals threads that wait for an event
    // threads that are signaled enter in the prio queue
    // return number of threads unlocked

    if (io >= scheduler->io_supported || scheduler_state == UNINITIALISED) {
        return EXIT_ERROR;
    }

    // queue all threads for given io => signal those threads
    signal_row(io);
    
    int result = row_size[io];
    row_size[io] = 0;

    so_exec();

    return result;
}

int change_threads() {   

    queue_thread main = scheduler->main_thread;

    // change main thread to first in queue
    scheduler->main_thread = scheduler->queue[0];
    scheduler->main_thread_id = scheduler->queue[0].thread_id;
    scheduler->main_thread.time_quantum = scheduler->quantum;
    scheduler->main_thread.status = RUNNING;

    main.time_quantum = scheduler->quantum;

    int first_in_queue_id = scheduler->queue[0].thread_id;
    // enqueue main thread in place of the first
    queue_remove();
    queue_add(main);

    // execute first thread in queue now
    if (sem_post(&semaphores[first_in_queue_id]))
        return EXIT_ERROR;

    return 0;
}

int schedule_case_one() {
    // schedule thread when the prio queue is not empty

    // if status is termianted or waiting set new thread
    if (scheduler->main_thread.status == TERMINATED || scheduler->main_thread.status == WAITING) {
        if (set_main_thread_from_queue())
            return EXIT_ERROR;
        return 0;
    }


    // there is higher prio candidate available
    if (scheduler->queue[0].prio > scheduler->main_thread.prio)
    {
        if (change_threads())
            return EXIT_ERROR;
        return 0;
    }


    // time quantum expired
    if (scheduler->main_thread.time_quantum <= 0)
    {
        // if there is a thread with equal prio
        if (scheduler->main_thread.prio == scheduler->queue[0].prio)
        {
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

int schedule_case_two() {
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

int decide_thread() {
    // if current thread is finished and in queue we dont have a candidate

    if (scheduler->main_thread_id != -1) {
        if (scheduler->main_thread.status == TERMINATED && !scheduler->size)
            return 0;
    }

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
            err = schedule_case_one();
            if (err < 0)
                return EXIT_ERROR;
            else
                return 0;
        } else {
            err = schedule_case_two();
            if (err < 0)
                return EXIT_ERROR;
            else
                return 0;
        }
    }
    return 0;
}

void so_exec() {

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

void *start_thread(void *args) {
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

    if (thread->thread_id == 0) {
        if (sem_post(&sem_term))
            return NULL;
    }

    return NULL;
}

tid_t so_fork(so_handler *handler, unsigned int prio) {
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

    if (pthread_mutex_lock(&mutex))
        return EXIT_ERROR;

    // enqueue new thread
    queue_add(scheduler->list_of_threads[last]);
    scheduler->no_threads++;

    if (pthread_mutex_unlock(&mutex))
        return EXIT_ERROR;

    so_exec();

    return scheduler->threads[last];
}

void so_end() {
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

    free_resources();
    free(semaphores);

    // set state to uinitialised
    scheduler_state = UNINITIALISED;
}
