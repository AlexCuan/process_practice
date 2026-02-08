#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

typedef struct
{
    int id;
    pid_t pid;
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
        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) name = argv[++i];
            else { fprintf(stderr, "Error: --name requires a value.\n"); return 1; }
        }
        else if (strcmp(argv[i], "--map") == 0) {
            if (i + 1 < argc) map_file = argv[++i];
            else { fprintf(stderr, "Error: --map requires a file path.\n"); return 1; }
        }
        else if (strcmp(argv[i], "--ships") == 0) {
            if (i + 1 < argc) ships_file = argv[++i];
            else { fprintf(stderr, "Error: --ships requires a file path.\n"); return 1; }
        }
        else if (strcmp(argv[i], "--random") == 0) {
            random_mode = 1;
        }
    }


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
    for(int i=0; i<100; i++) launched_ships[i].pid = 0;

    fprintf(stderr, "Captain Name: %s\n", name);

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

            pid_t pid = fork();
            if (pid < 0) {
                perror("Fork failed");
                continue;
            }

            if (pid == 0) // Child Process
            {
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

                execl(ship_exec, "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file, NULL);

                fprintf(stderr, "Failed to execute %s\n", ship_exec);
                perror("execl failed");
                exit(1);
            }
            else
            {
                int placed = 0;
                for(int i=0; i<100; i++){
                    if(launched_ships[i].pid == 0){
                        launched_ships[i].id = id;
                        launched_ships[i].pid = pid;
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


    fprintf(stderr, "[Captain] Waiting for ships to finish...\n");

    while (ships_count > 0) {
        pause(); // Sleeps until a signal is received (SIGCHLD or SIGINT)
    }

    fprintf(stderr, "[Captain] All ships have returned. Terminating execution.\n");
    return 0;
}