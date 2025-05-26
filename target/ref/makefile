# Define the C compiler
CC = gcc

# Define compiler flags (e.g., -Wall for all warnings)
CFLAGS = -Wall -pedantic -g -DBCI_TRACE

SRCS1 = \
../../src/bci/vm.c \
../../src/bci/bci.c \
../../src/bci/bciHW.c \
../../src/RS-232/rs232.c \
../../src/mole/mole.c \
../../src/mole/blake2s.c \
../../src/mole/xchacha.c \
../../src/KMS/kms.c \
main.c

OBJS1 = $(SRCS1:.c=.o)

all:	targ

targ:	$(OBJS1)
	$(CC) -o $@ $^ $(CFLAGS)
	@echo	To test: ./targ [port] [baudrate]


# Phony target for cleaning up
clean:
	-rm -f $(OBJS1) targ *.Identifier

# make all      makes targ
# make clean    remove object files

# Note that you can't make both targ and ok in the same makefile.
# Their .o files are generated with and without BCI_TRACE defined.
