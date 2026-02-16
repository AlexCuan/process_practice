#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include "map.h"

// Structure to manage ship state
typedef struct
{
    int id;
    pid_t pid;          // System PID

    // Pipe establishment
    int pipe_to_ship[2];   // Captain writes to [1], Ship reads from [0] (stdin)
    int pipe_from_ship[2]; // Ship writes to [1] (stdout), Captain reads from [0]
    FILE *read_stream;     // FILE* wrapper for pipe_from_ship[0] for getline usage
    int x, y;              // Track position for collision detection
    int active;            // 1 if alive, 0 if finished
} ShipRecord;

ShipRecord launched_ships[100];
int ships_count = 0;

// Signal handler for child process termination
void handle_sigchld(int sig) {
    (void)sig;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        int finished_id = -1;
        for(int i = 0; i < 100; i++) {
            if (launched_ships[i].pid == pid) {
                finished_id = launched_ships[i].id;
                launched_ships[i].pid = 0;
                launched_ships[i].active = 0;

                close(launched_ships[i].pipe_to_ship[1]);
                close(launched_ships[i].pipe_from_ship[0]);
                if (launched_ships[i].read_stream) {
                    fclose(launched_ships[i].read_stream);
                    launched_ships[i].read_stream = NULL;
                }
                break;
            }
        }

        if (finished_id != -1) {
            ships_count--;

            if (WIFEXITED(status)) {
                int gold_collected = WEXITSTATUS(status);
                fprintf(stderr, "[Captain] Ship %d (PID %d) has finished. Treasures collected: %d\n",
                       finished_id, pid, gold_collected);
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "[Captain] Ship %d (PID %d) was sunk by signal %d.\n",
                       finished_id, pid, WTERMSIG(status));
            }
        }
    }
}

// Signal handler for interrupts (Ctrl+C)
void handle_sigint(int sig) {
    (void)sig;
    fprintf(stderr, "\n[Captain] SIGINT signal received! Ordering withdrawal (SIGQUIT) to all ships...\n");

    for (int i = 0; i < 100; i++) {
        if (launched_ships[i].pid > 0 && launched_ships[i].active) {
            kill(launched_ships[i].pid, SIGQUIT);
        }
    }
}

int main(int argc, char* argv[])
{
    char* name = "Captain Amina al-Sirafi";
    char* map_file = "map.txt";
    char* ships_file = "ships.txt";
    int random_mode = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcasecmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) name = argv[++i];
            else { fprintf(stderr, "Error: --name requires a value.\n"); return 1; }
        }
        else if (strcasecmp(argv[i], "--map") == 0) {
            if (i + 1 < argc) map_file = argv[++i];
            else { fprintf(stderr, "Error: --map requires a file path.\n"); return 1; }
        }
        else if (strcasecmp(argv[i], "--ships") == 0) {
            if (i + 1 < argc) ships_file = argv[++i];
            else { fprintf(stderr, "Error: --ships requires a file path.\n"); return 1; }
        }
        else if (strcasecmp(argv[i], "--random") == 0) {
            random_mode = 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);

    // Setup signal handlers
    struct sigaction sa_int, sa_chld;

    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);


    // Initialize ships array
    for(int i=0; i<100; i++) {
        launched_ships[i].pid = 0;
        launched_ships[i].active = 0;
        launched_ships[i].read_stream = NULL;
    }

    fprintf(stderr, "Captain Name: %s PID: %d\n", name, getpid());

    // Load Map
    Map *map = map_load(map_file);
    if (!map) {
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
    ssize_t read_len;

    // Parsing lines from ships file
    // Uses getline for reading and sscanf for parsing
    while ((read_len = getline(&line, &len, file)) != -1)
    {
        if (read_len <= 1) continue;

        if (sscanf(line, "%d (%d,%d) %d", &id, &x, &y, &speed) == 4)
        {
            fprintf(stderr,"Launching Ship ID: %d, Position: (%d, %d)\n", id, x, y);

            // Create pipes
            int p_to_s[2];   // Write to ship
            int p_from_s[2]; // Read from ship
            if (pipe(p_to_s) == -1 || pipe(p_from_s) == -1) {
                perror("pipe failed");
                continue;
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("Fork failed");
                continue;
            }

            if (pid == 0) // Child Process
            {
                // Redirect STDIN/STDOUT to pipes
                dup2(p_to_s[0], STDIN_FILENO);   // Read from parent
                dup2(p_from_s[1], STDOUT_FILENO); // Write to parent

                // Close unused pipe ends
                close(p_to_s[0]);
                close(p_to_s[1]);
                close(p_from_s[0]);
                close(p_from_s[1]);

                // Close inherited pipes from previously launched ships
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        close(launched_ships[i].pipe_to_ship[1]);
                        close(launched_ships[i].pipe_from_ship[0]);
                    }
                }

                signal(SIGINT, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                char x_str[12], y_str[12], speed_str[12];
                snprintf(x_str, sizeof(x_str), "%d", x);
                snprintf(y_str, sizeof(y_str), "%d", y);
                snprintf(speed_str, sizeof(speed_str), "%d", speed);

                // Execute ship
                if (random_mode)
                    execl("./ship", "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file, NULL);
                else
                    execl("./ship", "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, NULL);

                perror("execl failed");
                exit(1);
            }
            else // Parent Process
            {
                close(p_to_s[0]);   // Close read end of write pipe
                close(p_from_s[1]); // Close write end of read pipe

                int placed = 0;
                for(int i=0; i<100; i++){
                    if(launched_ships[i].pid == 0){
                        launched_ships[i].id = id;
                        launched_ships[i].pid = pid;
                        launched_ships[i].x = x;
                        launched_ships[i].y = y;
                        launched_ships[i].pipe_to_ship[1] = p_to_s[1];
                        launched_ships[i].pipe_from_ship[0] = p_from_s[0];
                        // Convert FD to FILE* for getline usage
                        launched_ships[i].read_stream = fdopen(p_from_s[0], "r");
                        launched_ships[i].active = 1;
                        placed = 1;
                        break;
                    }
                }
                if(placed) ships_count++;
            }
        }
    }

    free(line);
    fclose(file);

    // Main Control Loop
    if (random_mode) {
        fprintf(stderr, "[Captain] Waiting for ships to finish (Random Mode)...\n");
        while (ships_count > 0) {
            pause();
        }
    } else {
        // Buffers for getline usage
        char *cmd_line = NULL;
        size_t cmd_len = 0;
        char *resp_line = NULL;
        size_t resp_len = 0;

        while (ships_count > 0) {
            // Prompt to stderr
            fprintf(stderr, "Introduce command [exit | status | <id> up/down/right/left]: ");

            // Read command from user using getline (stdin)
            // Note: getline still reads from stdin (keyboard/pipe input),
            // but prompts and feedbacks go to stderr.
            if (getline(&cmd_line, &cmd_len, stdin) == -1) {
                break;
            }

            // Remove newline
            cmd_line[strcspn(cmd_line, "\n")] = 0;
            if (strlen(cmd_line) == 0) continue;

            if (strcasecmp(cmd_line, "exit") == 0) {
                fprintf(stderr, "Exiting and terminating all ships.\n");
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        kill(launched_ships[i].pid, SIGQUIT);
                    }
                }
                while (ships_count > 0) {
                    pause();
                }
                break;
            }
            else if (strcasecmp(cmd_line, "status") == 0) {
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        // 1. Trigger status print in ship
                        kill(launched_ships[i].pid, SIGTSTP);

                        // 2. Read response using getline
                        ssize_t n = getline(&resp_line, &resp_len, launched_ships[i].read_stream);

                        if (n > 0) {
                            // Parsing status response from Ship
                            int s_pid, s_x, s_y, s_food, s_gold;
                            if (sscanf(resp_line, "PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d",
                                       &s_pid, &s_x, &s_y, &s_food, &s_gold) == 5) {
                                fprintf(stderr, "Ship %d alive (PID: %d) Location: (%d, %d) Food: %d Gold: %d\n",
                                       launched_ships[i].id, s_pid, s_x, s_y, s_food, s_gold);
                            } else {
                                fprintf(stderr, "Raw Status from Ship %d: %s", launched_ships[i].id, resp_line);
                            }
                        }
                    }
                }
                fprintf(stderr, "Number of ships alive: %d\n", ships_count);
            }
            else {
                // Parsing User Command
                int target_id;
                char action[32];
                if (sscanf(cmd_line, "%d %31s", &target_id, action) == 2) {
                    int found_idx = -1;
                    for (int i = 0; i < 100; i++) {
                        if (launched_ships[i].active && launched_ships[i].id == target_id) {
                            found_idx = i;
                            break;
                        }
                    }

                    if (found_idx != -1) {
                        if (strcasecmp(action, "exit") == 0) {
                            fprintf(stderr, "Sending exit action to ship %d...\n", target_id);
                            // Command to pipe
                            dprintf(launched_ships[found_idx].pipe_to_ship[1], "exit\n");
                        }
                        else if (strcasecmp(action, "up") == 0 || strcasecmp(action, "down") == 0 ||
                                 strcasecmp(action, "left") == 0 || strcasecmp(action, "right") == 0) {

                            // Calculate potential new position
                            int dx = 0, dy = 0;
                            if (strcasecmp(action, "up") == 0) dy = -1;
                            if (strcasecmp(action, "down") == 0) dy = 1;
                            if (strcasecmp(action, "left") == 0) dx = -1;
                            if (strcasecmp(action, "right") == 0) dx = 1;

                            int new_x = launched_ships[found_idx].x + dx;
                            int new_y = launched_ships[found_idx].y + dy;

                            // Check collision with other ships
                            int collision = 0;
                            for (int i = 0; i < 100; i++) {
                                if (launched_ships[i].active && launched_ships[i].id != target_id) {
                                    if (launched_ships[i].x == new_x && launched_ships[i].y == new_y) {
                                        collision = 1;
                                        break;
                                    }
                                }
                            }

                            if (collision) {
                                fprintf(stderr, "Cannot realize %sward movement action for ship %d (collision).\n", action, target_id);
                            } else {
                                // Check map bounds/rocks
                                if (!map_can_sail(map, new_x, new_y)) {
                                    fprintf(stderr, "Cannot move %s: Destination is blocked/rock.\n", action);
                                } else {
                                    // 1. Send Command
                                    dprintf(launched_ships[found_idx].pipe_to_ship[1], "%s\n", action);

                                    // 2. Wait for OK/NOK using getline
                                    ssize_t n = getline(&resp_line, &resp_len, launched_ships[found_idx].read_stream);

                                    if (n > 0) {
                                        // Trim newline
                                        resp_line[strcspn(resp_line, "\n")] = 0;

                                        if (strcmp(resp_line, "OK") == 0) {
                                            // 3. Update position ONLY if confirmed
                                            launched_ships[found_idx].x = new_x;
                                            launched_ships[found_idx].y = new_y;
                                            fprintf(stderr, "Ship %d moved %s to (%d, %d)\n", target_id, action, new_x, new_y);
                                        } else {
                                            fprintf(stderr, "Ship %d rejected move (NOK - likely insufficient food)\n", target_id);
                                        }
                                    }
                                }
                            }
                        } else {
                            fprintf(stderr, "Unknown command: %s\n", action);
                        }
                    } else {
                        fprintf(stderr, "Ship %d not found or not alive.\n", target_id);
                    }
                    fprintf(stderr, "Number of ships alive: %d\n", ships_count);
                }
            }
        }

        // Free buffers used by getline
        if (cmd_line) free(cmd_line);
        if (resp_line) free(resp_line);
    }

    fprintf(stderr, "[Captain] Waiting for ships to finish...\n");
    while (ships_count > 0) {
        pause();
    }

    fprintf(stderr, "[Captain] All ships have returned. Terminating execution.\n");
    map_destroy(map);
    return 0;
}