CC?=gcc
CFLAGS=-Wall -g -c -std=gnu11
BINS=cat16 cat16.a

all: $(BINS)
cat16:
	$(CC) $(CFLAGS) cat16.c
cat16.a:
	ar -cvrs cat16.a cat16.o 
clean:
	rm -rf *.a *.o *.tgz