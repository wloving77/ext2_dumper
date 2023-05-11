CC = gcc
CFLAGS = -Wall -Wextra -std=c99

all: fsdump

fsdump: fsdump.c ext2_fs.h
	$(CC) $(CFLAGS) -o fsdump fsdump.c

clean:
	rm -f fsdump