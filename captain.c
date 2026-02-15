#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include "map.h"

typedef struct
{
    int id;
    pid_t pid;

    // Pipe establishment
    int pipe_to_ship[2];   // Captain writes, Ship reads (stdin)
    int pipe_from_ship[2]; // Ship writes (stdout), Captain reads
    int x, y;              // Track position for collision detection
    int active;            // 1 if alive, 0 if finished
} ShipRecord;


ShipRecord launched_ships[100];
int ships_count = 0;


void handle_sigchld(int sig) {
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


void handle_sigint(int sig) {
    fprintf(stderr, "\n[Captain] SIGINT signal received! Ordering withdrawal (SIGQUIT) to all ships...\n");

    for (int i = 0; i < 100; i++) {
        if (launched_ships[i].pid > 0) {
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

    // Recommended way using sigaction to avoid old SysV behaviors
    struct sigaction sa_int, sa_chld;

    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0; // 0 So it does not restart what was doing
    sigaction(SIGINT, &sa_int, NULL);

    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART; // So if a signal from a child enters, doesn't stop what is doing at the time
    sigaction(SIGCHLD, &sa_chld, NULL);


    // Initialize ships array
    for(int i=0; i<100; i++) {
        launched_ships[i].pid = 0;
        launched_ships[i].active = 0;;
    }

    fprintf(stderr, "Captain Name: %s\n", name);

    // Collision detection
    Map *map = map_load(map_file);
    if (!map) {
        fprintf(stderr, "Error loading map %s\n", map_file);
        return 1;
    }

    FILE* file = fopen(ships_file, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening ships file: %s\n", ships_file);
        return 1;
    }

    int id, x, y, speed;
    char* ship_exec = "./cmake-build-debug/ship";
    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, file)) != -1)
    {
        if (read <= 1) continue;

        if (sscanf(line, "%d (%d,%d) %d", &id, &x, &y, &speed) == 4)
        {
            fprintf(stderr,"Launching Ship ID: %d, Position: (%d, %d)\n", id, x, y);

            // Pipe creation
            int p_to_s[2];
            int p_from_s[2];
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
                // Routing in and out through pipes
                dup2(p_to_s[0], STDIN_FILENO);
                dup2(p_from_s[1], STDOUT_FILENO);

                close(p_to_s[0]);
                close(p_to_s[1]);
                close(p_from_s[0]);
                close(p_from_s[1]);

                // Close inherited pipes from previously launched ships to prevent deadlocks
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        close(launched_ships[i].pipe_to_ship[1]);
                        close(launched_ships[i].pipe_from_ship[0]);
                    }
                }

                // Restore default signal in child so it doesn't inherit captain's handler
                // (Although execl overwrites memory space, it's good practice in pure forks)
                signal(SIGINT, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                char x_str[12];
                char y_str[12];
                char speed_str[12];

                snprintf(x_str, sizeof(x_str), "%d", x);
                snprintf(y_str, sizeof(y_str), "%d", y);
                snprintf(speed_str, sizeof(speed_str), "%d", speed);

                if (random_mode)
                    execl("./ship", "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file, NULL);
                else
                    execl("./ship", "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, NULL);

                fprintf(stderr, "Failed to execute %s\n", ship_exec);
                perror("execl failed");
                exit(1);
            }
            else
            {
                int placed = 0;

                close(p_to_s[0]); // Captain won't read from stdin pipe
                close(p_from_s[1]); // Captain won't write to stdout pipe

                for(int i=0; i<100; i++){
                    if(launched_ships[i].pid == 0){
                        launched_ships[i].id = id;
                        launched_ships[i].pid = pid;
                        launched_ships[i].x = x;
                        launched_ships[i].y = y;
                        launched_ships[i].pipe_to_ship[1] = p_to_s[1];
                        launched_ships[i].pipe_from_ship[0] = p_from_s[0];
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

    if (random_mode) {
        fprintf(stderr, "[Captain] Waiting for ships to finish...\n");
        while (ships_count > 0) {
            pause();
        }
    } else {
        char cmd_line[256];
        while (ships_count > 0) {
            printf("Introducir comando [exit | status | (Num, up/down/right/left/exit]: ");
            fflush(stdout);

            // Read standard input
            if (fgets(cmd_line, sizeof(cmd_line), stdin) == NULL) {
                break; // Break on EOF or error
            }

            // Trim newline character
            cmd_line[strcspn(cmd_line, "\n")] = 0;
            if (strlen(cmd_line) == 0) continue;

            if (strcasecmp(cmd_line, "exit") == 0) {
                printf("Saliendo y terminando todos los barcos.\n");
                // Issue SIGQUIT to all active ships
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        kill(launched_ships[i].pid, SIGQUIT);
                    }
                }
                // Wait for all processes to properly close
                while (ships_count > 0) {
                    pause();
                }
                break;
            }
            else if (strcasecmp(cmd_line, "status") == 0) {
                for (int i = 0; i < 100; i++) {
                    if (launched_ships[i].active) {
                        // Request status by triggering the ship's SIGTSTP handler
                        kill(launched_ships[i].pid, SIGTSTP);

                        char buf[512];
                        int n;
                        int total_read = 0;

                        // Robustly read a single line from the pipe (until '\n')
                        do {
                            n = read(launched_ships[i].pipe_from_ship[0], buf + total_read, 1);
                            if (n > 0) {
                                total_read += n;
                                if (buf[total_read - 1] == '\n') break;
                            }
                        } while ((n > 0 || (n == -1 && errno == EINTR)) && total_read < sizeof(buf) - 1);

                        // Parse Ship stdout payload and format to match requirement
                        if (total_read > 0) {
                            buf[total_read] = '\0';
                            int pid, x, y, food, gold;
                            if (sscanf(buf, "PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d", &pid, &x, &y, &food, &gold) == 5) {
                                printf("Barco %d Vivo (ID: %d, PID: %d) En: (%d, %d) Comida: %d Oro: %d\n",
                                       launched_ships[i].id, launched_ships[i].id, pid, x, y, food, gold);
                            }
                        }
                    }
                }
                printf("número de barcos vivos: %d\n", ships_count);
            }
            else {
                // Targeted Ship Command Parsing
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
                            printf("Enviando acción exit al barco %d\n", target_id);
                            // Push string securely through the pipe to the ship's stdin
                            dprintf(launched_ships[found_idx].pipe_to_ship[1], "exit\n");
                        }
                        else if (strcasecmp(action, "up") == 0 || strcasecmp(action, "down") == 0 ||
                                 strcasecmp(action, "left") == 0 || strcasecmp(action, "right") == 0) {

                            // Map shift calculations
                            int dx = 0, dy = 0;
                            if (strcasecmp(action, "up") == 0) dy = -1;
                            if (strcasecmp(action, "down") == 0) dy = 1;
                            if (strcasecmp(action, "left") == 0) dx = -1;
                            if (strcasecmp(action, "right") == 0) dx = 1;

                            int new_x = launched_ships[found_idx].x + dx;
                            int new_y = launched_ships[found_idx].y + dy;

                            // Collision assessment with other ships
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
                                printf("Mover %s para barco %d no es posible debido a colisión.\n", action, target_id);
                            } else {
                                // If legal non-rock space, lock coordinates locally to guarantee synch with ship
                                if (map_can_sail(map, new_x, new_y)) {
                                    launched_ships[found_idx].x = new_x;
                                    launched_ships[found_idx].y = new_y;
                                }
                                dprintf(launched_ships[found_idx].pipe_to_ship[1], "%s\n", action);
                            }
                        } else {
                            printf("Comando desconocido.\n");
                        }
                    } else {
                        printf("Barco %d no encontrado o no está vivo.\n", target_id);
                    }
                    printf("Número de barcos vivos: %d\n", ships_count);
                }
            }
        }
    }

    fprintf(stderr, "[Captain] Waiting for ships to finish...\n");

    while (ships_count > 0) {
        pause(); // Sleeps until a signal is received (SIGCHLD or SIGINT)
    }

    fprintf(stderr, "[Captain] All ships have returned. Terminating execution.\n");
    return 0;
}