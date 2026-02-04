#ifndef MAP_H
#define MAP_H

// Definición de constantes para la leyenda del mapa
#define WATER '.'
#define ROCK '#'
#define PORT 'P'
#define ISLAND 'I'
#define SHIP 'S'
#define HOME 'H'
#define BAR 'B'

// Estructura que representa el mapa
typedef struct {
    char **data;
    int width;      
    int height;    
} Map;

// Funciones públicas
Map* map_load(const char *filename);
void map_destroy(Map *map);
int map_can_sail(Map *map, int x, int y);
char map_get_cell_type(Map *map, int x, int y);
int map_set_ship(Map *map, int x, int y);
void map_remove_ship(Map *map, int x, int y);
void map_print(Map *map); // Para imprimirlo en stderr (debug)

#endif