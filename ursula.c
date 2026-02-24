#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define MAX_SHIPS 1000
#define MAX_CAPTAINS 100

typedef struct {
    int pid;
    int x;
    int y;
    int food;
    int gold;
    int active;
} ShipInfo;

typedef struct {
    int pid;
    int active;
} CaptainInfo;

ShipInfo ships[MAX_SHIPS];
CaptainInfo captains[MAX_CAPTAINS];
int treasury = 100;
char *global_fifo_path = NULL;


void handle_sigint_ursula(int sig) {
    (void)sig;
    fprintf(stdout, "\n[Ursula] Limpiando...\n");
    if (global_fifo_path) {
        unlink(global_fifo_path);
    }
    exit(EXIT_SUCCESS);
}

/**
 * @brief Encuentra el índice de un barco en el array de barcos basado en su PID.
 * Itera a través del array de barcos, buscando un barco activo con un PID coincidente. Si lo encuentra, devuelve el índice;
 * de lo contrario, devuelve -1 para indicar que el barco no fue encontrado.
 * @param pid El ID del proceso del barco a encontrar.
 * @return El índice del barco en el array de barcos si se encuentra, o -1 si no se encuentra.
 */
int find_ship_index(int pid) {
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].active && ships[i].pid == pid) return i;
    }
    return -1;
}

/**
 * @brief Añade un nuevo barco al array de barcos con los parámetros dados.
 * Busca una ranura inactiva en el array de barcos, y si la encuentra, inicializa la información del barco (PID,
 * posición, comida, oro) y lo marca como activo. La función devuelve el índice del barco recién añadido o -1 si no
 * hay ranuras disponibles.
 * @param pid El ID del proceso del nuevo barco.
 * @param x La coordenada x inicial del barco.
 * @param y La coordenada y inicial del barco.
 * @param food La cantidad inicial de comida para el barco.
 * @param gold La cantidad inicial de oro para el barco.
 * @return El índice del barco recién añadido en el array de barcos, o -1 si no hay ranuras disponibles.
 */
int add_ship(int pid, int x, int y, int food, int gold) {
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (!ships[i].active) {
            ships[i].pid = pid;
            ships[i].x = x;
            ships[i].y = y;
            ships[i].food = food;
            ships[i].gold = gold;
            ships[i].active = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Encuentra el índice de un capitán en el array de capitanes basado en su PID.
 * Itera a través del array de capitanes, buscando un capitán activo con un PID coincidente. Si lo encuentra, devuelve el
 * índice; de lo contrario, devuelve -1 para indicar que el capitán no fue encontrado.
 * @param pid El ID del proceso del capitán a encontrar.
 * @return El índice del capitán en el array de capitanes si se encuentra, o -1 si no se encuentra.
 */
int find_captain_index(int pid) {
    for (int i = 0; i < MAX_CAPTAINS; i++) {
        if (captains[i].active && captains[i].pid == pid) return i;
    }
    return -1;
}

/**
 * @brief Añade un nuevo capitán al array de capitanes con el PID dado.
 * Busca una ranura inactiva en el array de capitanes, y si la encuentra, inicializa la información del capitán (PID)
 * y lo marca como activo. La función devuelve el índice del capitán recién añadido o -1 si no hay ranuras disponibles.
 * @param pid El ID del proceso del nuevo capitán.
 * @return El índice del capitán recién añadido en el array de capitanes, o -1 si no hay ranuras disponibles.
 */
int add_captain(int pid) {
    for (int i = 0; i < MAX_CAPTAINS; i++) {
        if (!captains[i].active) {
            captains[i].pid = pid;
            captains[i].active = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Resuelve el combate entre barcos situados en las mismas coordenadas (x, y).
 * La función identifica a todos los barcos en la ubicación especificada, selecciona aleatoriamente a un ganador entre ellos, y procesa a los
 * perdedores decrementando su comida y oro. El botín de los perdedores se junta en un pozo, y el ganador es recompensado
 * con oro de este pozo. Si el pozo es insuficiente para recompensar al ganador, Ursula subsidia la diferencia de su
 * tesoro. Si el tesoro no puede cubrir el subsidio, envía una señal a todos los capitanes para terminar y sale del programa.
 * @param x La coordenada x de la ubicación del combate.
 * @param y La coordenada y de la ubicación del combate.
 */
void resolve_combat(int x, int y) {
    int combatants[MAX_SHIPS];
    int count = 0;

    // Identificar barcos en esta ubicación
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].active && ships[i].x == x && ships[i].y == y) {
            combatants[count++] = i;
        }
    }

    if (count < 2) return; // No se necesita pelear

    fprintf(stdout, "[Ursula] ¡Combate en (%d, %d) entre %d barcos!\n", x, y, count);

    // Escoger un ganador
    int winner_idx_in_combatants = rand() % count;
    int winner_ship_idx = combatants[winner_idx_in_combatants];

    int loot_pool = 0;

    // Procesar Perdedores
    for (int i = 0; i < count; i++) {
        if (i == winner_idx_in_combatants) continue;

        int loser_idx = combatants[i];

        // Decrementar Comida (estado interno de Ursula)
        if (ships[loser_idx].food >= 10) {
            ships[loser_idx].food -= 10;
        } else {
            ships[loser_idx].food = 0;
        }

        // Decrementar Oro (Transferir al pozo - estado interno de Ursula)
        if (ships[loser_idx].gold >= 10) {
            ships[loser_idx].gold -= 10;
            loot_pool += 10;
        } else {
            // Tomar lo que tengan
            loot_pool += ships[loser_idx].gold;
            ships[loser_idx].gold = 0;
        }

        kill(ships[loser_idx].pid, SIGUSR2);

        fprintf(stdout, "[Ursula] Barco %d perdió el combate. Comida: %d, Oro: %d.\n",
                ships[loser_idx].pid, ships[loser_idx].food, ships[loser_idx].gold);
    }

    // Recompensar Ganador
    int reward_needed = 10;

    kill(ships[winner_ship_idx].pid, SIGUSR1);

    // Si el pozo tiene suficiente, el ganador toma 10, el resto va a Ursula
    if (loot_pool >= reward_needed) {
        ships[winner_ship_idx].gold += reward_needed;
        int surplus = loot_pool - reward_needed;
        treasury += surplus;
        fprintf(stdout, "[Ursula] ¡Barco %d ganó! Recibió 10 de oro. Ursula cobró un impuesto de %d de oro.\n",
                ships[winner_ship_idx].pid, surplus);
    }
    // Si el pozo es insuficiente, Ursula paga la diferencia
    else {
        ships[winner_ship_idx].gold += reward_needed;
        int subsidy_needed = reward_needed - loot_pool;
        if (treasury >= subsidy_needed) {
            treasury -= subsidy_needed;
            fprintf(stdout, "[Ursula] ¡Barco %d ganó! Recibió 10 de oro (Subsidiado con %d). Tesoro: %d.\n",
                    ships[winner_ship_idx].pid, subsidy_needed, treasury);
        } else {
            // EL FIN DEL MUNDO
            fprintf(stderr, "[Ursula] ¡BANCARROTA DEL TESORO (%d)! No se puede pagar el subsidio de %d. EL FIN ESTÁ CERCA.\n",
                    treasury, subsidy_needed);

            // Matar a todos los capitanes
            for (int k = 0; k < MAX_CAPTAINS; k++) {
                if (captains[k].active) {
                    fprintf(stderr, "[Ursula] Señalizando al Capitán %d para que termine.\n", captains[k].pid);
                    kill(captains[k].pid, SIGINT);
                }
            }
            exit(EXIT_SUCCESS);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <nombre_fifo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    global_fifo_path = argv[1];

    if (signal(SIGINT, handle_sigint_ursula) == SIG_ERR) {
        perror("Error configurando SIGINT");
        return EXIT_FAILURE;
    }

    // Crear FIFO si no existe
    if (mkfifo(global_fifo_path, 0666) == -1) {
        if (errno != EEXIST) {
            perror("Error en mkfifo");
            return EXIT_FAILURE;
        }
    }

    fprintf(stdout, "[Ursula] La Dama del Mar (PID: %d) escuchando en %s. Tesoro: %d\n", getpid(), global_fifo_path, treasury);

    // Abrir FIFO
    FILE *fp = fopen(global_fifo_path, "r+");
    if (!fp) {
        perror("Error en fopen fifo");
        return EXIT_FAILURE;
    }

    // Initial random seed
    srand(time(NULL));

    // Inicializar arrays
    for(int i=0; i<MAX_SHIPS; i++) ships[i].active = 0;
    for(int i=0; i<MAX_CAPTAINS; i++) captains[i].active = 0;

    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;

    while (1) {
        read_len = getline(&line, &len, fp);
        if (read_len == -1) {
            clearerr(fp);
            continue;
        }

        if (read_len > 0 && line[read_len-1] == '\n') line[read_len-1] = '\0';
        if (strlen(line) == 0) continue;

        // Parseo de variables
        int pid, x, y, food, gold;
        char type[32];
        char *endptr; // Usado para strtol

        char *token = strtok(line, ",");
        if (!token) continue;
        pid = (int)strtol(token, &endptr, 10);

        token = strtok(NULL, ",");
        if (!token) continue;
        while (*token == ' ') token++; // Recortar espacio inicial
        strcpy(type, token);

        if (strcmp(type, "INIT_CAPT") == 0) {
            add_captain(pid);
            fprintf(stdout, "[Ursula] Capitán %d registrado.\n", pid);
        }
        else if (strcmp(type, "END_CAPT") == 0) {
            int idx = find_captain_index(pid);
            if (idx != -1) {
                captains[idx].active = 0;
                fprintf(stdout, "[Ursula] Capitán %d se ha desconectado.\n", pid);
            }
        }
        else if (strcmp(type, "TERMINATE") == 0) {
            int idx = find_ship_index(pid);
            if (idx != -1) {
                ships[idx].active = 0;
                fprintf(stdout, "[Ursula] Barco %d terminado.\n", pid);
            }
        }
        else if (strcmp(type, "INIT") == 0 || strcmp(type, "MOVE") == 0) {
            // Retrieve remaining tokens safely
            char *tok_x = strtok(NULL, ",");
            char *tok_y = strtok(NULL, ",");
            char *tok_food = strtok(NULL, ",");
            char *tok_gold = strtok(NULL, ",");

            if (tok_x && tok_y && tok_food && tok_gold) {
                x = (int)strtol(tok_x, &endptr, 10);
                y = (int)strtol(tok_y, &endptr, 10);
                food = (int)strtol(tok_food, &endptr, 10);
                gold = (int)strtol(tok_gold, &endptr, 10);

                int idx = find_ship_index(pid);

                if (strcmp(type, "INIT") == 0) {
                    if (idx == -1) add_ship(pid, x, y, food, gold);
                    fprintf(stdout, "[Ursula] Barco %d registrado en (%d, %d).\n", pid, x, y);
                }
                else { // MOVER
                    if (idx != -1) {
                        ships[idx].x = x;
                        ships[idx].y = y;
                        ships[idx].food = food;
                        ships[idx].gold = gold;
                        fprintf(stdout, "[Ursula] Barco %d se movió a (%d, %d). Comida: %d, Oro: %d.\n", pid, x, y, food, gold);

                        resolve_combat(x, y);
                    } else {
                        // Por si acaso algun init no llego...
                        fprintf(stderr, "[Ursula] ADVERTENCIA: Barco %d no estaba registrado...\n", pid);
                        add_ship(pid, x, y, food, gold);
                    }
                }
            }
        }

        // Comprobar Condición de Terminación Global
        static int ever_had_captains = 0;
        int active_c = 0;
        int active_s = 0;
        for(int i=0; i<MAX_CAPTAINS; i++) if(captains[i].active) active_c++;
        for(int i=0; i<MAX_SHIPS; i++) if(ships[i].active) active_s++;

        if (active_c > 0) ever_had_captains = 1;

        if (ever_had_captains && active_c == 0 && active_s == 0) {
            fprintf(stdout, "[Ursula] Todas las flotas han partido. El mar está en silencio.\n");
            break;
        }
    }

    if (line) free(line);
    fclose(fp);
    unlink(global_fifo_path);
    return EXIT_SUCCESS;
}