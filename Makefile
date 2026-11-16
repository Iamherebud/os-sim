# Build:   make          Run tests:  make test       Clean:  make clean
# Needs Linux (or WSL on Windows): uses pthreads, fork() and pipes.

CC      = gcc
CFLAGS  = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 -Iinclude
LDFLAGS = -pthread -lm

LIB_SRC = src/scheduler.c src/memory.c src/workload.c src/bench.c src/ipc.c
LIB_OBJ = $(LIB_SRC:.c=.o)

all: ossim

ossim: $(LIB_OBJ) src/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

tests/test_sim: $(LIB_OBJ) tests/test_sim.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c include/*.h
	$(CC) $(CFLAGS) -pthread -c $< -o $@

test: tests/test_sim
	./tests/test_sim

clean:
	rm -f src/*.o tests/*.o ossim tests/test_sim

.PHONY: all test clean
