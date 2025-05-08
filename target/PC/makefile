# Define the C compiler
CC = gcc

# Define compiler flags (e.g., -Wall for all warnings)
CFLAGS = -Wall -g

SRCS1 = ../../src/bci/bci.c \
../../src/bci/bciHW.c \
../../src/RS-232/rs232.c \
../../src/mole/src/mole.c \
../../src/mole/src/blake2s/src/blake2s.c \
../../src/mole/src/xchacha/src/xchacha.c \
../../src/host/tools.c \
main.c

OBJS1 = $(SRCS1:.c=.o)

all:	targ

targ:	$(OBJS1)
	$(CC) -o $@ $^ $(CFLAGS)
	# @echo	To test: ./targ <port> <baudrate>


# Phony target for cleaning up
clean:
	-rm -f $(OBJS1) targ *.Identifier

# make all      makes targ
# make clean    remove object files
