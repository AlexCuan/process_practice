CC = gcc
CFLAGS = -Wall -Wextra -g

# The 'all' target is what runs when you just type 'make'
all: ship captain ursula

# Compile the ship executable. It needs map.c because it uses map_load [cite: 100]
ship: ship.c map.c map.h
	$(CC) $(CFLAGS) ship.c map.c -o ship

# Compile the captain executable. It also needs map.c for collision detection [cite: 101]
captain: captain.c map.c map.h
	$(CC) $(CFLAGS) captain.c map.c -o captain

# Compile Ursula. It's a standalone program.
ursula: ursula.c
	$(CC) $(CFLAGS) ursula.c -o ursula

# Clean up compiled binaries
clean:
	rm -f ship captain ursula