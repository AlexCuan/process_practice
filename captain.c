#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

typedef struct
{
    int id;
    pid_t pid;
} ShipRecord;


int main(int argc, char* argv[])
{
    char* name = "Capitana Amina al-Sirafi";
    char* map_file = "map.txt";
    char* ships_file = "ships.txt";
    int random_mode = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--name") == 0)
        {
            if (i + 1 < argc)
            {
                name = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: --name requires a value.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--map") == 0)
        {
            if (i + 1 < argc)
            {
                map_file = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: --map requires a file path.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--ships") == 0)
        {
            if (i + 1 < argc)
            {
                ships_file = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: --ships requires a file path.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--random") == 0)
        {
            random_mode = 1;
        }
        else
        {
            printf("Warning: Unknown argument %s\n", argv[i]);
        }
    }
    ShipRecord launched_ships[100];
    int ships_count = 0;
    
    fprintf(stderr, "Captain Name: %s\n", name);

    FILE* file = fopen(ships_file, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening ships file: %s\n", ships_file);
        return 1;
    }

    int id, x, y, speed;
    int children_count = 0;
    char* ship_exec = "./cmake-build-debug/ship";

    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, file)) != -1)
    {
        if (read <= 1) continue;

        if (sscanf(line, "%d (%d,%d) %d", &id, &x, &y, &speed) == 4)
        {

            fprintf(stderr,"Barco ID: %d, Posición: (%d, %d), Velocidad: %d\n", id, x, y, speed);

            pid_t pid = fork();
            if (pid < 0)
            {
                perror("Fork failed");
                continue;
            }

            if (pid == 0)
            {
                char x_str[12];
                char y_str[12];
                char speed_str[12];

                snprintf(x_str, sizeof(x_str), "%d", x);
                snprintf(y_str, sizeof(y_str), "%d", y);
                snprintf(speed_str, sizeof(speed_str), "%d", speed);

                execl(ship_exec, "ship", "--pos", x_str, y_str, "--random", "5", speed_str, "--map", map_file, NULL);

                fprintf(stderr, "Failed to execute %s\n", ship_exec);
                perror("execl failed");
                exit(1);
            }
            else
            {
                fprintf(stderr,"Barco PID: %d\n", pid);
                launched_ships[ships_count].id = id;
                launched_ships[ships_count].pid = pid;
                ships_count++;
            }
        }
        else
        {
            fprintf(stderr, "Skipping invalid line in ships file: %s", line);
        }
    }

    free(line);
    fclose(file);

    int status;
    pid_t finished_pid;

    while (ships_count > 0) {
        finished_pid = wait(&status);
        if (finished_pid > 0) {
            ships_count--;

            int finished_id = -1;
            for(int i=0; i < 100; i++) {
                if (launched_ships[i].pid == finished_pid) {
                    finished_id = launched_ships[i].id;
                    break;
                }
            }

            if (WIFEXITED(status)) {
                int gold_collected = WEXITSTATUS(status);
                fprintf(stderr,"Barco %d, con pid %d ha terminado con estado %d\n",
                       finished_id, finished_pid, gold_collected);
            }
        }
    }

    return 0;
}
