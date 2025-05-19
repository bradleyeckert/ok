# Define the C compiler
CC = gcc

# Define compiler flags (e.g., -Wall for all warnings)
CFLAGS = -Wall -g

SRCS1 = src/bci/vm.c \
src/bci/bci.c \
src/bci/bciHW.c \
src/RS-232/rs232.c \
src/mole/src/mole.c \
src/mole/src/blake2s/src/blake2s.c \
src/mole/src/xchacha/src/xchacha.c \
src/host/tools.c \
src/host/see.c \
src/host/comm.c \
src/host/quit.c \
src/host/forth.c \
src/host/main.c

OBJS1 = $(SRCS1:.c=.o)

all:	ok

ok:	$(OBJS1)
	$(CC) -o $@ $^ $(CFLAGS)
	@echo	To test: ./ok include test.f


# Phony target for cleaning up
clean:
	-rm -f $(OBJS1) ok *.Identifier

# make all      makes ok
# make clean    remove object files
