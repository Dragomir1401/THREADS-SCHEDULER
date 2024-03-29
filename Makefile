CC = gcc
CFLAGS = -g -fPIC -pthread -Wall -Wextra
LDFLAGS = -m32

.PHONY: build
build: libscheduler.so

libscheduler.so: threads_scheduler.o
	$(CC) $(CFLAGS) -shared -o libscheduler.so threads_scheduler.o

so_scheduler.o: threads_scheduler.c queue.c so_scheduler.h scheduler_struct.h
	$(CC) $(CFLAGS) -o threads_scheduler.o -c threads_scheduler.c

.PHONY: clean
clean:
	-rm -rf threads_scheduler.o queue.o libscheduler.so