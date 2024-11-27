BIN := await
CC := x86_64-linux-musl-gcc
CFLAGS := -Wall -Wextra -Wpedantic -std=c2x -O2 -static -s

all: main

main: main.c
	$(CC) $(CFLAGS) -o $(BIN) main.c

clean:
	rm -f $(BIN) main.o
