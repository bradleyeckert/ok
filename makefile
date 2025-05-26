# Define the C compiler
CC = gcc

# Define compiler flags (e.g., -Wall for all warnings)
CFLAGS = -Wall -pedantic -g

SRCS1 = src/bci/vm.c \
src/bci/bci.c \
src/bci/bciHW.c \
src/RS-232/rs232.c \
src/mole/mole.c \
src/mole/blake2s.c \
src/mole/xchacha.c \
src/KMS/kms.c \
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
