#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler_struct.h"
#include "constants.h"

void add(queue_thread t) {
    // find the place in queue for the new thread
    unsigned int index = 0;
    for (unsigned int i = 0; i < scheduler->size; i++) {
        if (t.prio > scheduler->queue[i].prio) {
            index = i;
            break;
        }
    }

    // shift the other threads
    for (unsigned int i = scheduler->size; i >= index + 1; i--) {
        scheduler->queue[i] = scheduler->queue[i - 1];
    }

    // place the new thread
    scheduler->queue[index] = t;
    scheduler->size++;
}

void delete() {
    // deletes first element from queue
    for (unsigned int i = 0; i < scheduler->size - 1; i++) {
        scheduler->queue[i] = scheduler->queue[i + 1];
    }

    scheduler->size--;
}

int set_main_thread_from_queue() {
    queue_thread new_thread = scheduler->queue[0];

    scheduler->main_thread = new_thread;
    scheduler->main_thread_id = new_thread.thread_id;
    scheduler->quantum = new_thread.time_quantum;
    scheduler->main_thread.status = RUNNING_STATE;

    delete();

    // execute thread
    // value non zero is error
    int error = sem_post(&semaphores[scheduler->main_thread_id]);
    if (error)
        return error;

    return 0;
}
