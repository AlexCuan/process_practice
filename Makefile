CC = gcc
CFLAGS = -Wall -Wextra -g

all: ship captain ursula

ship: ship.c map.c map.h
	$(CC) $(CFLAGS) ship.c map.c -o ship

captain: captain.c map.c map.h
	$(CC) $(CFLAGS) captain.c map.c -o captain

ursula: ursula.c
	$(CC) $(CFLAGS) ursula.c -o ursula

clean:
	rm -f ship captain ursula
