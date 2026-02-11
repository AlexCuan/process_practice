#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include "map.h"
#include <signal.h>

// Direcciones: {dx, dy} -> Abajo, Derecha, Arriba, Izquierda
int directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

// Estructura para gestionar el estado del barco
typedef struct {
    Map *mapa;
    int x;
    int y;
    int food;
    int gold;
    pid_t pid;
} Ship;

// Global variables for Signal Handlers
Ship *aux_ship = NULL;
int ship_speed = 1;
int steps_remaining = -1;

// Forward declaration
void move_randomly(Ship *s);
void ship_print(Ship *s);

// Check for special events (if ship docks or arrives at an island)
void check_event(Ship *s) {
    char cell_type = map_get_cell_type(s->mapa, s->x, s->y);
    if (cell_type == BAR) {
        s->gold += 10;
        fprintf(stderr, "Barco %d ha alcanzado una isla (%d, %d), oro incrementado a %d.\n", s->pid, s->x, s->y, s->gold);
    } else if (cell_type == HOME) {
        s->food += 20;
        fprintf(stderr, "Barco %d ha atracado con un puerto (%d, %d), comida aumentada a %d.\n", s->pid, s->x, s->y, s->food);
    }
}

// Signal management
void sigusr1_handler(int signal){
    if (aux_ship != NULL) {
        aux_ship->gold += 10;
        fprintf(stderr, "Señal USR1 recibida: +10 Oro (Total: %d)\n", aux_ship->gold);
    }
}

void sigusr2_handler(int signal){
    if (aux_ship != NULL) {
        if(aux_ship->gold >= 10){
            aux_ship->gold -= 10;
        } else {
            aux_ship->gold = 0;
        }

        if(aux_ship->food >= 10){
            aux_ship->food -= 10;
        } else {
            aux_ship->food = 0;
        }
        fprintf(stderr, "Señal USR2 recibida: ¡Ataque! Comida: %d, Oro: %d\n",
            aux_ship->food, aux_ship->gold);
    }
}

void sigquit_handler(int signal) {
    if (aux_ship != NULL) {
        int end_gold = aux_ship->gold;
        fprintf(stderr, "Barco %d ha terminado con estado %d (SIGQUIT).\n",
            aux_ship->pid, end_gold);
        if (aux_ship->mapa) map_destroy(aux_ship->mapa);
        exit(end_gold);
    }
    exit(0);
}

void sigstp_handler(int signal) {
    if (aux_ship != NULL) {
        printf("PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d\n",
               aux_ship->pid, aux_ship->x, aux_ship->y, aux_ship->food, aux_ship->gold);
        fflush(stdout);
    }
}

void sigalrm_handler(int signal) {
    if (aux_ship != NULL) {
        if (steps_remaining == 0) {
            fprintf(stderr, "Barco %d ha terminado sus pasos aleatorios.\n", aux_ship->pid);
            exit(aux_ship->gold);
        }

        if (aux_ship->food < 5) {
             fprintf(stderr, "Barco %d no tiene suficiente comida para moverse.\n", aux_ship->pid);

        } else {
            int dir_idx = rand() % 4;
            // directions: {{0, 1} (Down), {1, 0} (Right), {0, -1} (Up), {-1, 0} (Left)}
            // Map coordinates often: x (col), y (row).
            // Let's ensure standard logical mapping: Up(y-1), Down(y+1), Left(x-1), Right(x+1)
            // The `directions` array above is:
            // 0: dx=0, dy=1  -> Down
            // 1: dx=1, dy=0  -> Right
            // 2: dx=0, dy=-1 -> Up
            // 3: dx=-1, dy=0 -> Left

            int dx = directions[dir_idx][0];
            int dy = directions[dir_idx][1];
            int new_x = aux_ship->x + dx;
            int new_y = aux_ship->y + dy;

            if (map_can_sail(aux_ship->mapa, new_x, new_y)) {
                map_remove_ship(aux_ship->mapa, aux_ship->x, aux_ship->y);
                aux_ship->x = new_x;
                aux_ship->y = new_y;
                map_set_ship(aux_ship->mapa, aux_ship->x, aux_ship->y);

                aux_ship->food -= 5;
                check_event(aux_ship);

                fprintf(stderr, "Barco %d en (%d, %d) con %d comida y %d oro.\n",
                        aux_ship->pid, aux_ship->x, aux_ship->y, aux_ship->food, aux_ship->gold);
            }
        }

        if (steps_remaining > 0) {
            steps_remaining--;
        }
        alarm(ship_speed);
    }
}

void setup_signals() {
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        perror("Error setting up SIGUSR1");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR) {
        perror("Error setting up SIGUSR2");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, sigquit_handler) == SIG_ERR) {
        perror("Error setting up SIGQUIT");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTSTP, sigstp_handler) == SIG_ERR) {
        perror("Error setting up SIGTSTP");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGALRM, sigalrm_handler) == SIG_ERR) {
        perror("Error setting up SIGALRM");
        exit(EXIT_FAILURE);
    }
}

// Command Mode Helper
void shift_position(Ship *s, int shift_x, int shift_y) {
    if (s->food < 5) {
        fprintf(stderr, "Barco %d sin comida suficiente.\n", s->pid);
        printf("NOK\n");
        fflush(stdout);
        return;
    }

    int new_x = s->x + shift_x;
    int new_y = s->y + shift_y;

    if (map_can_sail(s->mapa, new_x, new_y)) {
        map_remove_ship(s->mapa, s->x, s->y);
        s->x = new_x;
        s->y = new_y;
        map_set_ship(s->mapa, s->x, s->y);
        s->food -= 5;

        check_event(s);


        map_print(s->mapa);

        printf("OK\n");
        fflush(stdout);
        fprintf(stderr, "Barco %d en (%d, %d) con %d comida y %d oro.\n",
                s->pid, s->x, s->y, s->food, s->gold);
    }
    else {
        printf("NOK\n");
        fflush(stdout);
        fprintf(stderr, "Movimiento bloqueado para barco %d.\n", s->pid);
    }
}

void command_mode(Ship *s) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    alarm(0);   // Disable the alarm

    fprintf(stderr, "Barco PID: %d. Modo captain\n", s->pid);

    // Read lines from stdin until EOF or error
    while ((nread = getline(&line, &len, stdin)) != -1) {

        // Remove the newline character at the end if it exists
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        if (strcasecmp(line, "up") == 0) {
            shift_position(s, 0, -1);
        }
        else if (strcasecmp(line, "down") == 0) {
            shift_position(s, 0, 1);
        }
        else if (strcasecmp(line, "left") == 0) {
            shift_position(s, -1, 0);
        }
        else if (strcasecmp(line, "right") == 0) {
            shift_position(s, 1, 0);
        }
        else if (strcasecmp(line, "exit") == 0) {
            fprintf(stderr, "Barco %d saliendo con oro %d.\n", s->pid, s->gold);
            free(line);
            exit(s->gold);
        }
        else {
            if (strlen(line) > 0) { // Ignore empty lines
                fprintf(stderr, "Comando desconocido: %s\n", line);
            }
        }
    }
    free(line);
}

// Inicialización barco
void ship_init(Ship *s, Map *mapa, int x, int y, int food) {
    s->mapa = mapa;
    s->x = x;
    s->y = y;
    s->food = food;
    s->gold = 0;
    s->pid = getpid();
    map_set_ship(s->mapa, s->x, s->y);
}

void ship_print(Ship *s) {
    fprintf(stderr, "Barco %d en (%d, %d) con %d comida y %d oro.\n", s->pid, s->x, s->y, s->food, s->gold);
}

void move_randomly(Ship *s) {
    if (s->food < 5) {
        fprintf(stderr, "No hay suficiente comida para moverse.\n");
        return;
    }

    int dir_idx = rand() % 4;
    int dx = directions[dir_idx][0];
    int dy = directions[dir_idx][1];
    int new_x = s->x + dx;
    int new_y = s->y + dy;

    if (map_can_sail(s->mapa, new_x, new_y)) {
        map_remove_ship(s->mapa, s->x, s->y);
        s->x = new_x;
        s->y = new_y;
        map_set_ship(s->mapa, s->x, s->y);
        s->food -= 5;

        char cell_type = map_get_cell_type(s->mapa, s->x, s->y);
        if (cell_type == BAR) {
            s->gold += 10;
            fprintf(stderr, "Barco %d llegó a una isla (%d, %d), oro aumentado a %d.\n", s->pid, s->x, s->y, s->gold);
        } else if (cell_type == HOME) {
            s->food += 20;
            fprintf(stderr, "Barco %d llegó a un puerto (%d, %d), comida aumentada a %d.\n", s->pid, s->x, s->y, s->food);
        }
    }
}

// Parsing arguments
static int parse_args(int argc, char *argv[], char **map_file, int *pos_x, int *pos_y,
                      int *food, int *random_steps, int *random_speed, int *use_captain) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            *map_file = argv[++i];
        } else if (strcmp(argv[i], "--pos") == 0 && i + 2 < argc) {
            char *end;
            long v;

            // Parse X
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --pos X: %s\n", argv[i]);
                return 1;
            }
            *pos_x = (int)v;

            // Parse Y
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --pos Y: %s\n", argv[i]);
                return 1;
            }
            *pos_y = (int)v;

        } else if (strcmp(argv[i], "--food") == 0 && i + 1 < argc) {
            char *end;
            long v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --food: %s\n", argv[i]);
                return 1;
            }
            *food = (int)v;

        } else if (strcmp(argv[i], "--random") == 0 && i + 2 < argc) {
            char *endptr;
            long val;

            // Parse Steps
            errno = 0;
            val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
                fprintf(stderr, "Valor inválido para --random pasos: %s\n", argv[i]);
                return 1;
            }
            *random_steps = (int)val;

            // Parse Speed
            errno = 0;
            val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
                fprintf(stderr, "Valor inválido para --random velocidad: %s\n", argv[i]);
                return 1;
            }
            *random_speed = (int)val;

        } else if (strcmp(argv[i], "--captain") == 0) {
            *use_captain = 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {

    char *map_file = "map.txt";

    int pos_x = 1;
    int pos_y = 1;
    int food = 100;
    int random_steps = -1;
    int random_speed = 1;
    int use_captain = 0;

    if (parse_args(argc, argv, &map_file, &pos_x, &pos_y, &food, &random_steps, &random_speed, &use_captain) != 0) {
        return 1;
    }

    fprintf(stderr, "Mapa: %s, Posición: (%d, %d), Comida: %d\n", map_file, pos_x, pos_y, food);

    if (random_steps == -1 && !use_captain) {
        fprintf(stderr, "Error: Debe especificar --random N s1 o --captain\n");
        return 1;
    }
    if (use_captain && random_steps != -1) {
        fprintf(stderr, "Error: No puede usar --captain y --random a la vez.\n");
        return 1;
    }

    Map *mapa = map_load(map_file);
    if (!mapa) {
        fprintf(stderr, "Error cargando el mapa: %s\n", map_file);
        return 1;
    }

    Ship ship;
    if (map_can_sail(mapa, pos_x, pos_y)) {
        ship_init(&ship, mapa, pos_x, pos_y, food);
    } else {
        fprintf(stderr, "Posición inicial (%d, %d) inválida.\n", pos_x, pos_y);
        map_destroy(mapa);
        exit(1);
    }

    fprintf(stderr, "Barco PID: %d\n", ship.pid);

    aux_ship = &ship;
    ship_speed = random_speed;
    steps_remaining = random_steps;

    setup_signals();
    srand(time(NULL) ^ getpid());

    if (use_captain) {
        // FIXED: Now we correctly call the function to read commands [cite: 186-187]
        command_mode(&ship);
    } else {
        alarm(ship_speed);
        while (1) {
            pause();
        }
    }

    map_destroy(mapa);
    return ship.gold;
}