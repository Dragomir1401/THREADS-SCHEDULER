#include "so_scheduler.h"
#include <pthread.h>
#include <semaphore.h>


typedef struct queue_thread {
    // priority of the thread
    unsigned prio;

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
    unsigned int size;

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

// global declarations of structures
static scheduler_struct *scheduler;
static sem_t *semaphores;
static sem_t sem_term;
static pthread_mutex_t mutex;
static unsigned scheduler_state;
// matrix to store threads waiting on every operation
static queue_thread **thread_mat;
static int *row_size;