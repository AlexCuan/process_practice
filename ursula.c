#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

// Helper to find or add entities
int find_ship_index(int pid) {
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].active && ships[i].pid == pid) return i;
    }
    return -1;
}

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

int find_captain_index(int pid) {
    for (int i = 0; i < MAX_CAPTAINS; i++) {
        if (captains[i].active && captains[i].pid == pid) return i;
    }
    return -1;
}

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

// Combat Logic
void resolve_combat(int x, int y) {
    int combatants[MAX_SHIPS];
    int count = 0;

    // 1. Identify ships at this location
    for (int i = 0; i < MAX_SHIPS; i++) {
        if (ships[i].active && ships[i].x == x && ships[i].y == y) {
            combatants[count++] = i;
        }
    }

    if (count < 2) return; // No fight needed

    fprintf(stdout, "[Ursula] Combat at (%d, %d) between %d ships!\n", x, y, count);

    // 2. Pick a winner
    int winner_idx_in_combatants = rand() % count;
    int winner_ship_idx = combatants[winner_idx_in_combatants];

    int loot_pool = 0;

    // 3. Process Losers
    for (int i = 0; i < count; i++) {
        if (i == winner_idx_in_combatants) continue;

        int loser_idx = combatants[i];

        // Decrement Food
        if (ships[loser_idx].food >= 10) {
            ships[loser_idx].food -= 10;
        } else {
            ships[loser_idx].food = 0;
        }

        // Decrement Gold (Transfer to pool)
        if (ships[loser_idx].gold >= 10) {
            ships[loser_idx].gold -= 10;
            loot_pool += 10;
        } else {
            // Take whatever they have
            loot_pool += ships[loser_idx].gold;
            ships[loser_idx].gold = 0;
        }

        fprintf(stdout, "[Ursula] Ship %d lost combat. Food: %d, Gold: %d.\n",
                ships[loser_idx].pid, ships[loser_idx].food, ships[loser_idx].gold);
    }

    // 4. Reward Winner
    int reward_needed = 10;

    // If pool has enough, winner takes 10, rest goes to Ursula
    if (loot_pool >= reward_needed) {
        ships[winner_ship_idx].gold += reward_needed;
        int surplus = loot_pool - reward_needed;
        treasury += surplus;
        fprintf(stdout, "[Ursula] Ship %d won! Gained 10 gold. Ursula taxed %d gold.\n",
                ships[winner_ship_idx].pid, surplus);
    }
    // If pool is insufficient, Ursula pays the difference
    else {
        ships[winner_ship_idx].gold += reward_needed;
        int subsidy_needed = reward_needed - loot_pool;
        if (treasury >= subsidy_needed) {
            treasury -= subsidy_needed;
            fprintf(stdout, "[Ursula] Ship %d won! Gained 10 gold (Subsidized by %d). Treasury: %d.\n",
                    ships[winner_ship_idx].pid, subsidy_needed, treasury);
        } else {
            // THE END OF THE WORLD
            fprintf(stderr, "[Ursula] TREASURY BANKRUPT (%d)! Cannot pay subsidy %d. THE END IS NIGH.\n",
                    treasury, subsidy_needed);

            // Kill all captains
            for (int k = 0; k < MAX_CAPTAINS; k++) {
                if (captains[k].active) {
                    fprintf(stderr, "[Ursula] Signaling Captain %d to terminate.\n", captains[k].pid);
                    kill(captains[k].pid, SIGINT);
                }
            }
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fifo_name>\n", argv[0]);
        return 1;
    }

    char *fifo_path = argv[1];

    // Create FIFO if it doesn't exist
    if (mkfifo(fifo_path, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            return 1;
        }
    }

    fprintf(stdout, "[Ursula] Lady of the Sea (PID: %d) listening on %s. Treasury: %d\n", getpid(), fifo_path, treasury);

    // Open FIFO
    FILE *fp = fopen(fifo_path, "r+");
    if (!fp) {
        perror("fopen fifo");
        return 1;
    }

    // Initial random seed
    srand(time(NULL));

    // Initialize arrays
    for(int i=0; i<MAX_SHIPS; i++) ships[i].active = 0;
    for(int i=0; i<MAX_CAPTAINS; i++) captains[i].active = 0;

    char *line = NULL;
    size_t len = 0;
    ssize_t read_len;

    while (1) {
        read_len = getline(&line, &len, fp);
        if (read_len == -1) {
            clearerr(fp);
            usleep(100000);
            continue;
        }

        if (read_len > 0 && line[read_len-1] == '\n') line[read_len-1] = '\0';
        if (strlen(line) == 0) continue;

        // Parsing variables
        int pid, x, y, food, gold;
        char type[32];
        char *endptr; // Used for strtol

        // 1. PID
        char *token = strtok(line, ",");
        if (!token) continue;
        pid = (int)strtol(token, &endptr, 10);

        // 2. Type
        token = strtok(NULL, ",");
        if (!token) continue;
        while (*token == ' ') token++; // trim leading space
        strcpy(type, token);

        if (strcmp(type, "INIT_CAPT") == 0) {
            add_captain(pid);
            fprintf(stdout, "[Ursula] Captain %d registered.\n", pid);
        }
        else if (strcmp(type, "END_CAPT") == 0) {
            int idx = find_captain_index(pid);
            if (idx != -1) {
                captains[idx].active = 0;
                fprintf(stdout, "[Ursula] Captain %d signed off.\n", pid);
            }
        }
        else if (strcmp(type, "TERMINATE") == 0) {
            int idx = find_ship_index(pid);
            if (idx != -1) {
                ships[idx].active = 0;
                fprintf(stdout, "[Ursula] Ship %d terminated.\n", pid);
            }
        }
        else if (strcmp(type, "INIT") == 0 || strcmp(type, "MOVE") == 0) {
            // Retrieve remaining tokens safely
            char *tok_x = strtok(NULL, ",");
            char *tok_y = strtok(NULL, ",");
            char *tok_food = strtok(NULL, ",");
            char *tok_gold = strtok(NULL, ",");

            if (tok_x && tok_y && tok_food && tok_gold) {
                // Convert using strtol
                x = (int)strtol(tok_x, &endptr, 10);
                y = (int)strtol(tok_y, &endptr, 10);
                food = (int)strtol(tok_food, &endptr, 10);
                gold = (int)strtol(tok_gold, &endptr, 10);

                int idx = find_ship_index(pid);

                if (strcmp(type, "INIT") == 0) {
                    if (idx == -1) add_ship(pid, x, y, food, gold);
                    fprintf(stdout, "[Ursula] Ship %d registered at (%d, %d).\n", pid, x, y);
                }
                else { // MOVE
                    if (idx != -1) {
                        ships[idx].x = x;
                        ships[idx].y = y;
                        ships[idx].food = food;
                        ships[idx].gold = gold;
                        fprintf(stdout, "[Ursula] Ship %d moved to (%d, %d). Food: %d, Gold: %d.\n", pid, x, y, food, gold);

                        resolve_combat(x, y);
                    } else {
                        // Ship moved but we didn't know it? Add it just in case
                        add_ship(pid, x, y, food, gold);
                    }
                }
            }
        }

        // Check Global Termination Condition
        static int ever_had_captains = 0;
        int active_c = 0;
        int active_s = 0;
        for(int i=0; i<MAX_CAPTAINS; i++) if(captains[i].active) active_c++;
        for(int i=0; i<MAX_SHIPS; i++) if(ships[i].active) active_s++;

        if (active_c > 0) ever_had_captains = 1;

        if (ever_had_captains && active_c == 0 && active_s == 0) {
            fprintf(stdout, "[Ursula] All fleets have departed. The sea is silent.\n");
            break;
        }
    }

    if (line) free(line);
    fclose(fp);
    unlink(fifo_path);
    return 0;
}