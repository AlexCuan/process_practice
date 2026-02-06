#include <stdio.h>
#include <string.h>
#include "map.h"


int main(int argc, char *argv[]) {
    char *name = "Capitana Amina al-Sirafi";
    char *map_file = "map.txt";
    char *ships_file = "ships.txt";
    int random_mode = 0;

    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) {
                name = argv[++i]; // Increment i to consume the value
            } else {
                fprintf(stderr, "Error: --name requires a value.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--map") == 0) {
            if (i + 1 < argc) {
                map_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --map requires a file path.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--ships") == 0) {
            if (i + 1 < argc) {
                ships_file = argv[++i];
            } else {
                fprintf(stderr, "Error: --ships requires a file path.\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--random") == 0) {
            random_mode = 1;
        }
        else {
            printf("Warning: Unknown argument %s\n", argv[i]);
        }
    }

    fprintf(stderr, "Captain Name: %s\n", name);

    return 0;
}
