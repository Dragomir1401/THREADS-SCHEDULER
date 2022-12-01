#define LIMIT_IO 256
#define EXIT_ERROR -1
#define MAX 500
#define UNINITIALISED 0
#define INITIALISED 1
#define NEW_STATE 1
#define READY_STATE 2
#define RUNNING_STATE 3
#define WAITING_STATE 4
#define TERMINATED_STATE 5

void add(queue_thread t);
void delete();
int set_main_thread_from_queue();