#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"

Map* map_load(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;

    Map *map = malloc(sizeof(Map));
    if (!map) {
        fclose(f);
        return NULL;
    }
    map->height = 0;
    map->width = 0;
    map->data = NULL;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    // Leemos línea a línea
    while ((read = getline(&line, &len, f)) != -1) {
        // Eliminar el salto de línea al final si existe
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }
        
        int current_width = strlen(line);
        if (current_width > 0) {
            
            // Si es la primera línea, definimos el ancho del mapa
            if (map->height == 0) {
                map->width = current_width;
            } 
            // Si no es la primera, verificamos que el ancho coincida
            else if (current_width != map->width) {
                fprintf(stderr, "Error: Todas las filas deben tener la misma longitud\n");
                // Limpieza de memoria en caso de error
                free(line);
                map_destroy(map);
                fclose(f);
                return NULL;
            }

            // Realojamos memoria para añadir la nueva fila
            char **new_data = realloc(map->data, sizeof(char*) * (map->height + 1));
            if (!new_data) {
                perror("Error reservando memoria para el mapa");
                free(line);
                map_destroy(map);
                fclose(f);
                return NULL;
            }
            map->data = new_data;

            // Guardamos la línea
            map->data[map->height] = strdup(line);
            map->height++;
        }
    }

    free(line);
    fclose(f);
    return map;
}

void map_destroy(Map *map) {
    if (!map) return;
    // Liberar cada fila individualmente
    if (map->data) {
        for (int i = 0; i < map->height; i++) {
            if (map->data[i]) free(map->data[i]);
        }
        // Liberar el array de punteros
        free(map->data);
    }
    // Liberar la estructura principal
    free(map);
}

int map_can_sail(Map *map, int x, int y) {
    if (x >= 0 && x < map->width && y >= 0 && y < map->height) {
        return map->data[y][x] != ROCK;
    }
    return 0;
}

char map_get_cell_type(Map *map, int x, int y) {
    if (x >= 0 && x < map->width && y >= 0 && y < map->height) {
        return map->data[y][x];
    }
    return 0;
}

int map_set_ship(Map *map, int x, int y) {
    if (x >= 0 && x < map->width && y >= 0 && y < map->height) {
        char current = map->data[y][x];
        if (current == WATER) map->data[y][x] = SHIP;
        else if (current == PORT) map->data[y][x] = HOME;
        else if (current == ISLAND) map->data[y][x] = BAR;
        return 1; // Éxito
    }
    return 0; // Fallo (fuera de límites)
}

void map_remove_ship(Map *map, int x, int y) {
    if (x >= 0 && x < map->width && y >= 0 && y < map->height) {
        char current = map->data[y][x];
        if (current == SHIP) map->data[y][x] = WATER;
        else if (current == HOME) map->data[y][x] = PORT;
        else if (current == BAR) map->data[y][x] = ISLAND;
    }
}

void map_print(Map *map) {
    if (!map || !map->data) return;
    for (int i = 0; i < map->height; i++) {
        fprintf(stderr, "%s\n", map->data[i]);
    }
}