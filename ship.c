#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include "map.h"

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

// Inicialización barco
void ship_init(Ship *s, Map *mapa, int x, int y, int food) {
    s->mapa = mapa;
    s->x = x;
    s->y = y;
    s->food = food;
    s->gold = 0;
    s->pid = getpid();
    // Pintamos el barco en el mapa
    map_set_ship(s->mapa, s->x, s->y);
}


void ship_print(Ship *s) {
    // Usamos stderr para mensajes de log
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

void move_captain(Ship *s) {
    // Para futuras implementaciones
}

/*
 * Función para parsear argumentos. Podría usar getopt, pero se hace manualmente para mayor claridad.
 */
static int parse_args(int argc, char *argv[], char **map_file, int *pos_x, int *pos_y, int *food, int *random_steps, 
                        int *random_speed, int *use_captain) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            *map_file = argv[++i];
        } else if (strcmp(argv[i], "--pos") == 0 && i + 2 < argc) {
            char *end;
            long v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --pos X: %s\n", argv[i]);
                return 1;
            }
            *pos_x = (int) v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --pos Y: %s\n", argv[i]);
                return 1;
            }
            *pos_y = (int) v;

        } else if (strcmp(argv[i], "--food") == 0 && i + 1 < argc) {
            char *end;
            long v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --food: %s\n", argv[i]);
                return 1;
            }
            *food = (int) v;
        } else if (strcmp(argv[i], "--random") == 0 && i + 2 < argc) {
            char *end;
            long v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --random pasos: %s\n", argv[i]);
                return 1;
            }
            *random_steps = (int)v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX) {
                fprintf(stderr, "Valor inválido para --random velocidad: %s\n", argv[i]);
                return 1;
            }
            *random_speed = (int)v;
        } else if (strcmp(argv[i], "--captain") == 0) {
            *use_captain = 1;
        }
    }
    return 0;
}
int main(int argc, char *argv[]) {

    // Valores por defecto
    // map_file apunta a un string literal

    char *map_file = "map.txt";
    int pos_x = 0;
    int pos_y = 0;
    int food = 100;
    int random_steps = -1;
    int random_speed = 1;
    int use_captain = 0;

    // Parseo de argumentos
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

    //Carga del mapa 
    Map *mapa = map_load(map_file);
    if (!mapa) {
        fprintf(stderr, "Error cargando el mapa: %s\n", map_file);
        return 1;
    }

    //Verificar posición inicial y colcar barco
    Ship ship;
    if (map_can_sail(mapa, pos_x, pos_y)) {
        ship_init(&ship, mapa, pos_x, pos_y, food);
    } else {
        fprintf(stderr, "Posición inicial inválida.\n");
        map_destroy(mapa);
        exit(1);
    }
    
    fprintf(stderr, "Barco PID: %d\n", ship.pid);

    //Modo random o con capitan

    if (use_captain) {
        move_captain(&ship);
    } else {
        srand(time(NULL) ^ getpid()); // Semilla para rand
        for (int i = 0; i < random_steps; i++) {
            sleep(random_speed);
            move_randomly(&ship);
            ship_print(&ship);
            //map_print(mapa);//Opcional para hacerlo más visual
        }
    }

    fprintf(stderr, "Barco %d ha terminado con estado %d.\n", ship.pid, ship.gold);
    
    map_destroy(mapa);
    return ship.gold;
}