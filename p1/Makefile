CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g
SHIM_FLAGS ?= -std=c11 -Wall -Wextra -g

all: clemsh envshim.so
clemsh: clemsh.o parser.o
	$(CC) $(CFLAGS) -o $@ clemsh.o parser.o
clemsh.o: clemsh.c parser.h
	$(CC) $(CFLAGS) -c clemsh.c
parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c
envshim.so: envshim.c
	$(CC) $(SHIM_FLAGS) -fPIC -shared -o $@ envshim.c -ldl
run: clemsh
	./clemsh
clean:
	rm -f clemsh clemsh.o parser.o envshim.so *.txt