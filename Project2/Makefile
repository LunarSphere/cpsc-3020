CC?=gcc
CFLAGS=-Wall -g -c -std=gnu11
BINS=workqueue workqueue.a


all: $(BINS)
workqueue:
	$(CC) $(CFLAGS) workqueue.c
workqueue.a:
	ar -cvrs libworkqueue.a workqueue.o 
clean:
	rm -rf *.a *.o *.tgz
	