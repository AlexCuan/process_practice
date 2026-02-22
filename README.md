# Processes Assignment - Systems Architecture II (2025-2026)

This document contains the necessary instructions to compile and execute the code for this assignment.

## Compilation

To compile the three main programs (ship, captain, and Ursula), open a terminal in the directory containing the source files and run the following commands:

```bash
make
```

## Execution

The complete system requires running Ursula first in one terminal, and then the Captain in another terminal. The Captain will automatically handle spawning the ship processes.

### 1. Run Ursula

Open a terminal and run Ursula, providing the name that will be used for the named pipe (FIFO) for communication:

```bash
./ursula pipe_ursula
```
Ursula will remain active, waiting to receive messages from the captains and ships.

### 2. Run the Captain

Open a second terminal. The captain can be executed in two different modes:

**Random Mode:** Ships will move autonomously based on the steps and speed specified in the ships file.
```bash
./captain --name "Captain Amina" --map map.txt --ships ships.txt --random --ursula pipe_ursula
```

**Manual Mode (Commands):** The captain will prompt for keyboard commands to move the ships.
```bash
./captain --name "Captain Amina" --map map.txt --ships ships.txt --ursula pipe_ursula
```

**Available arguments for captain:**
* `--name <name>`: (Optional) Name of the captain.
* `--map <file>`: (Optional) Path to the map file (default: `map.txt`).
* `--ships <file>`: (Optional) Path to the ships information file (default: `ships.txt`).
* `--random`: (Optional) Enables automatic movement of the ships.
* `--ursula <fifo>`: (Optional) Name of the pipe to connect with Ursula. Must match the one used when launching `ursula`.

### 3. Individual Ship Execution

Although the Captain spawns ships internally using `exec`, you can launch a ship manually to debug its behavior:

**Random Mode:**
```bash
./ship --map map.txt --pos 1 3 --food 100 --random 10 1 --ursula pipe_ursula
```

**Captain Mode (waits for commands via stdin):**
```bash
./ship --map map.txt --pos 1 3 --food 100 --captain --ursula pipe_ursula
```

## Interaction in Manual Mode

If you run the Captain without the `--random` flag, the program will display a prompt requesting orders. The valid commands are:

* `<ship_id> up` : Moves the ship one cell up.
* `<ship_id> down` : Moves the ship one cell down.
* `<ship_id> left` : Moves the ship one cell to the left.
* `<ship_id> right` : Moves the ship one cell to the right.
* `<ship_id> exit` : Orders the specified ship to terminate its execution.
* `status` : Displays the state (PID, position, food, and gold) of all active ships.
* `exit` : Orders a retreat, terminating the execution of all ships and the captain.

# Documentation

This project includes a Doxygen configuration file (`Doxyfile`) to generate HTML documentation from the source code comments.

To generate the documentation, ensure you have Doxygen installed on your system and run the following command in the project root directory:

```bash
doxygen Doxyfile
```

Once the generation is complete, you can access the documentation by opening the resulting HTML index file in any web browser:

```bash
docs/html/index.html
```
