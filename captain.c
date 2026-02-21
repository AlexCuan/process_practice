/*
 * @file captain.c
 * @brief Controls the fleet logic and manages ship processes
 *
 * This program acts as the parent process. It spawns ships via fork/exec,
 * communicates commands via pipes, and synchronizes state with the
 * central server (Ursula).
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include "map.h"

// Global pipe to Ursula
FILE* ursula_pipe = NULL;

pid_t my_pid;

/**
 * @brief Structure to track launched ships and their communication channels
 */
typedef struct
{
    int id;
    pid_t pid;
    // Captain writes to [1], Ship reads from [0] (stdin)
    int pipe_to_ship[2];
    // Ship writes to [1] (stdout), Captain reads from [0]
    int pipe_from_ship[2];
    // FILE* wrapper for pipe_from_ship[0] for getline usage
    FILE* read_stream;
    // Track position for collision detection
    int x, y;
    // 1 if alive, 0 if finished
    int active;
} ShipRecord;

ShipRecord launched_ships[100];
int ships_count = 0;

/**
 * @brief Signal handler for SIGCHLD to detect when ships finish
 * * This handler uses waitpid with WNOHANG to reap child processes without blocking.
 * * It identifies which ship finished by matching the PID, updates the ship record,
 *   and prints the result (gold collected or if it was sunk).
 * @param sig Signal number (unused)
 */
void handle_sigchld(int sig)
{
    (void)sig;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int finished_id = -1;
        for (int i = 0; i < 100; i++)
        {
            if (launched_ships[i].pid == pid)
            {
                finished_id = launched_ships[i].id;
                launched_ships[i].pid = 0;
                launched_ships[i].active = 0;

                close(launched_ships[i].pipe_to_ship[1]);
                close(launched_ships[i].pipe_from_ship[0]);
                if (launched_ships[i].read_stream)
                {
                    fclose(launched_ships[i].read_stream);
                    launched_ships[i].read_stream = NULL;
                }
                break;
            }
        }

        if (finished_id != -1)
        {
            ships_count--;

            if (WIFEXITED(status))
            {
                int gold_collected = WEXITSTATUS(status);
                fprintf(stderr, "[Captain] Ship %d (PID %d) has finished. Treasures collected: %d\n",
                        finished_id, pid, gold_collected);
            }
            else if (WIFSIGNALED(status))
            {
                fprintf(stderr, "[Captain] Ship %d (PID %d) was sunk by signal %d.\n",
                        finished_id, pid, WTERMSIG(status));
            }
        }
    }
}

/**
 * @brief Signal handler for SIGINT to trigger orderly shutdown of all ships
 * * When the captain receives SIGINT (e.g., Ctrl+C), it sends a SIGQUIT signal to all active ships,
 *   instructing them to perform an orderly shutdown and report their final gold collected.
 * @param sig Signal number (unused)
 */
void handle_sigint(int sig)
{
    (void)sig;
    fprintf(stderr, "\n[Captain] SIGINT signal received! Ordering withdrawal (SIGQUIT) to all ships...\n");

    for (int i = 0; i < 100; i++)
    {
        if (launched_ships[i].pid > 0 && launched_ships[i].active)
        {
            kill(launched_ships[i].pid, SIGQUIT);
        }
    }
}

/**
 * @brief Cleanup function to notify Ursula of captain termination
 * * This function is registered with atexit to ensure that when the captain process exits,
 *   it sends a final message to Ursula indicating that the captain has finished.
 */
void cleanup_ursula()
{
    if (ursula_pipe)
    {
        fprintf(ursula_pipe, "%d,END_CAPT\n", my_pid);
        fflush(ursula_pipe);
        fclose(ursula_pipe);
    }
}

int main(int argc, char* argv[])
{
    my_pid = getpid();
    char* name = "Captain Amina al-Sirafi";
    char* map_file = "map.txt";
    char* ships_file = "ships.txt";
    char* ursula_fifo = NULL; // Path to Ursula's pipe
    int random_mode = 0;

    // TODO: Functionality of strcasecmp
    for (int i = 1; i < argc; i++)
    {
        if (strcasecmp(argv[i], "--name") == 0)
        {
            if (i + 1 < argc) name = argv[++i];
            else
            {
                fprintf(stderr, "Error: --name requires a value.\n");
                return 1;
            }
        }
        else if (strcasecmp(argv[i], "--map") == 0)
        {
            if (i + 1 < argc) map_file = argv[++i];
            else
            {
                fprintf(stderr, "Error: --map requires a file path.\n");
                return 1;
            }
        }
        else if (strcasecmp(argv[i], "--ships") == 0)
        {
            if (i + 1 < argc) ships_file = argv[++i];
            else
            {
                fprintf(stderr, "Error: --ships requires a file path.\n");
                return 1;
            }
        }
        else if (strcasecmp(argv[i], "--random") == 0)
        {
            random_mode = 1;
        }
        else if (strcasecmp(argv[i], "--ursula") == 0 && i + 1 < argc) ursula_fifo = argv[++i]; // Parse Ursula arg
    }

    // Handle broken pipes. When a ship process dies, writing to its pipe will cause SIGPIPE.
    // We want to ignore it and handle it gracefully.
    signal(SIGPIPE, SIG_IGN);

    // Setup signal handlers
    struct sigaction sa_int, sa_chld;


    sa_int.sa_handler = handle_sigint;
    // sigemptyset means "don't block any additional signals while handle_sigint is running"
    sigemptyset(&sa_int.sa_mask);
    // Setting it to 0 means "use default behavior." Specifically, it means we are not using flags like SA_RESTART.
    // If a system call (like read or pause) is interrupted by this signal, it will fail and return an error (EINTR)
    // rather than automatically restarting.
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = 0;
    sigaction(SIGCHLD, &sa_chld, NULL);
    sigaction(SIGCHLD, &sa_chld, NULL);


    // Initialize ships array
    for (int i = 0; i < 100; i++)
    {
        launched_ships[i].pid = 0;
        launched_ships[i].active = 0;
        launched_ships[i].read_stream = NULL;
    }

    fprintf(stderr, "Captain Name: %s PID: %d\n", name, my_pid);

    // Connect to Ursula if requested
    if (ursula_fifo)
    {
        ursula_pipe = fopen(ursula_fifo, "w");
        if (ursula_pipe)
        {
            // fprintf writes the message to the pipe directly
            fprintf(ursula_pipe, "%d,INIT_CAPT\n", my_pid);
            // fflush: This forces the C library to take whatever is currently in the buffer and immediately write it
            // to the file descriptor (the pipe).
            fflush(ursula_pipe);
            // Register cleanup to send END_CAPT on exit
            atexit(cleanup_ursula);
        }
        else
        {
            perror("Failed to open pipe to Ursula");
        }
    }

    Map* map = map_load(map_file);
    if (!map)
    {
        fprintf(stderr, "Error loading map %s\n", map_file);
        return 1;
    }

    // Load Ships info
    FILE* file = fopen(ships_file, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening ships file: %s\n", ships_file);
        return 1;
    }

    int id, x, y, speed;
    char* line = NULL;
    size_t len = 0;
    // We use ssize_t because getline returns can be -1 on error or EOF.
    // Using ssize_t allows us to properly check for these conditions.
    ssize_t read_len;

    // Parsing lines from ships file
    // Uses getline for reading and sscanf for parsing
    while ((read_len = getline(&line, &len, file)) != -1)
    {
        if (read_len <= 1) continue;

        if (sscanf(line, "%d (%d,%d) %d", &id, &x, &y, &speed) == 4)
        {
            fprintf(stderr, "Launching Ship ID: %d, Position: (%d, %d)\n", id, x, y);

            // Create pipes
            int p_to_s[2]; // Write to ship
            int p_from_s[2]; // Read from ship
            if (pipe(p_to_s) == -1 || pipe(p_from_s) == -1)
            {
                perror("pipe failed");
                continue;
            }

            pid_t pid = fork();
            if (pid < 0)
            {
                perror("Fork failed");
                continue;
            }

            if (pid == 0) // Child Process
            {
                // Redirect STDIN/STDOUT to pipes
                dup2(p_to_s[0], STDIN_FILENO); // Read from parent
                dup2(p_from_s[1], STDOUT_FILENO); // Write to parent

                // Close unused pipe ends
                close(p_to_s[0]);
                close(p_to_s[1]);
                close(p_from_s[0]);
                close(p_from_s[1]);

                // Close pipes belonging to PREVIOUSLY launched ships.
                // When fork() runs, this child inherits ALL open file descriptors
                // from the parent (Captain), including pipes connected to Ship 1, Ship 2, etc.
                // We must close these pipes here. If we don't,
                // the previous ships will never receive EOF (termination) signals
                // because this new child is accidentally keeping their pipes open.
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        close(launched_ships[i].pipe_to_ship[1]);
                        close(launched_ships[i].pipe_from_ship[0]);
                    }
                }

                // Close Ursula pipe in child (child will open its own connection)
                if (ursula_pipe) fclose(ursula_pipe);

                // Default signals
                signal(SIGINT, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                char x_str[12], y_str[12], speed_str[12];
                // Convert integers to strings for exec arguments
                snprintf(x_str, sizeof(x_str), "%d", x);
                snprintf(y_str, sizeof(y_str), "%d", y);
                snprintf(speed_str, sizeof(speed_str), "%d", speed);

                // Propagate --ursula arg to ship
                if (random_mode)
                {
                    // TODO: change hardcoded localtion of ship
                    if (ursula_fifo)
                        execl("./ship", "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file,
                              "--ursula", ursula_fifo, NULL);
                    else
                        execl("./ship", "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file,
                              NULL);
                }
                else
                {
                    if (ursula_fifo)
                        execl("./ship", "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, "--ursula",
                              ursula_fifo, NULL);
                    else
                        execl("./ship", "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, NULL);
                }

                perror("execl failed");
                exit(1);
            }
            else // Parent Process
            {
                // Close read end of write pipe
                close(p_to_s[0]);
                // Close write end of read pipe
                close(p_from_s[1]);

                int placed = 0;
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].pid == 0)
                    {
                        launched_ships[i].id = id;
                        launched_ships[i].pid = pid;
                        launched_ships[i].x = x;
                        launched_ships[i].y = y;
                        launched_ships[i].pipe_to_ship[1] = p_to_s[1];
                        launched_ships[i].pipe_from_ship[0] = p_from_s[0];
                        launched_ships[i].read_stream = fdopen(p_from_s[0], "r");
                        launched_ships[i].active = 1;
                        placed = 1;
                        break;
                    }
                }
                if (placed) ships_count++;
            }
        }
    }

    free(line);
    fclose(file);

    // Main Control Loop
    if (random_mode)
    {
        fprintf(stderr, "[Captain] Waiting for ships to finish (Random Mode)...\n");
        while (ships_count > 0)
        {
            pause();
        }
    }
    else
    {
        // Buffers for getline usage
        char* cmd_line = NULL;
        size_t cmd_len = 0;
        char* resp_line = NULL;
        size_t resp_len = 0;

        while (ships_count > 0)
        {
            // Prompt to stderr
            fprintf(stderr, "Introduce command [exit | status | <id> up/down/right/left]: ");

            if (getline(&cmd_line, &cmd_len, stdin) == -1)
            {
                if (errno == EINTR) {
                    clearerr(stdin);
                    continue; // A ship died, loop will re-evaluate ships_count > 0
                }
            }

            cmd_line[strcspn(cmd_line, "\n")] = 0;
            if (strlen(cmd_line) == 0) continue;

            if (strcasecmp(cmd_line, "exit") == 0)
            {
                fprintf(stderr, "Exiting and terminating all ships.\n");
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        kill(launched_ships[i].pid, SIGQUIT);
                    }
                }
                while (ships_count > 0)
                {
                    pause();
                }
                break;
            }
            else if (strcasecmp(cmd_line, "status") == 0)
            {
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        kill(launched_ships[i].pid, SIGTSTP);

                        ssize_t n = getline(&resp_line, &resp_len, launched_ships[i].read_stream);

                        if (n > 0)
                        {
                            // Parsing status response from Ship
                            int s_pid, s_x, s_y, s_food, s_gold;
                            if (sscanf(resp_line, "PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d",
                                       &s_pid, &s_x, &s_y, &s_food, &s_gold) == 5)
                            {
                                fprintf(stderr, "Ship %d alive (PID: %d) Location: (%d, %d) Food: %d Gold: %d\n",
                                        launched_ships[i].id, s_pid, s_x, s_y, s_food, s_gold);
                            }
                            else
                            {
                                fprintf(stderr, "Raw Status from Ship %d: %s", launched_ships[i].id, resp_line);
                            }
                        }
                    }
                }
                fprintf(stderr, "Number of ships alive: %d\n", ships_count);
            }
            else
            {
                // Parsing User Command
                int target_id;
                char action[32];
                if (sscanf(cmd_line, "%d %31s", &target_id, action) == 2)
                {
                    int found_idx = -1;
                    for (int i = 0; i < 100; i++)
                    {
                        if (launched_ships[i].active && launched_ships[i].id == target_id)
                        {
                            found_idx = i;
                            break;
                        }
                    }

                    if (found_idx != -1)
                    {
                        if (strcasecmp(action, "exit") == 0)
                        {
                            fprintf(stderr, "Sending exit action to ship %d...\n", target_id);
                            // Writes directly in the descriptor, that's the d from dprintf
                            // This is intended for unamed pipes, can't be used in Ursula since Ursula uses FIFOs
                            // (named pipes)
                            dprintf(launched_ships[found_idx].pipe_to_ship[1], "exit\n");
                        }
                        else if (strcasecmp(action, "up") == 0 || strcasecmp(action, "down") == 0 ||
                            strcasecmp(action, "left") == 0 || strcasecmp(action, "right") == 0)
                        {
                            int dx = 0, dy = 0;
                            if (strcasecmp(action, "up") == 0) dy = -1;
                            if (strcasecmp(action, "down") == 0) dy = 1;
                            if (strcasecmp(action, "left") == 0) dx = -1;
                            if (strcasecmp(action, "right") == 0) dx = 1;

                            int new_x = launched_ships[found_idx].x + dx;
                            int new_y = launched_ships[found_idx].y + dy;

                            // Check collision with other ships
                            int collision = 0;
                            for (int i = 0; i < 100; i++)
                            {
                                if (launched_ships[i].active && launched_ships[i].id != target_id)
                                {
                                    if (launched_ships[i].x == new_x && launched_ships[i].y == new_y)
                                    {
                                        collision = 1;
                                        break;
                                    }
                                }
                            }

                            if (collision)
                            {
                                fprintf(stderr, "Cannot realize %sward movement action for ship %d (collision).\n",
                                        action, target_id);
                            }
                            else
                            {
                                // Check map bounds/rocks
                                if (!map_can_sail(map, new_x, new_y))
                                {
                                    fprintf(stderr, "Cannot move %s: Destination is blocked/rock.\n", action);
                                }
                                else
                                {
                                    // Send Command
                                    dprintf(launched_ships[found_idx].pipe_to_ship[1], "%s\n", action);

                                    // Wait for OK/NOK using getline
                                    ssize_t n = getline(&resp_line, &resp_len, launched_ships[found_idx].read_stream);

                                    if (n > 0)
                                    {
                                        // Trim newline
                                        resp_line[strcspn(resp_line, "\n")] = 0;
                                        if (strcmp(resp_line, "OK") == 0)
                                        {
                                            // Update position ONLY if confirmed
                                            launched_ships[found_idx].x = new_x;
                                            launched_ships[found_idx].y = new_y;
                                            fprintf(stderr, "Ship %d moved %s to (%d, %d)\n", target_id, action, new_x,
                                                    new_y);
                                        }
                                        else
                                        {
                                            fprintf(stderr, "Ship %d rejected move\n",
                                                    target_id);
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            fprintf(stderr, "Unknown command: %s\n", action);
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Ship %d not found or not alive.\n", target_id);
                    }
                    fprintf(stderr, "Number of ships alive: %d\n", ships_count);
                }
            }
        }
        if (cmd_line) free(cmd_line);
        if (resp_line) free(resp_line);
    }

    fprintf(stderr, "[Captain] Waiting for ships to finish...\n");
    while (ships_count > 0)
    {
        pause();
    }

    fprintf(stderr, "[Captain] All ships have returned. Terminating execution.\n");
    map_destroy(map);
    return 0;
}
