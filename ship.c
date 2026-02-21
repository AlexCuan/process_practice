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

// Directions: Right, Down, Left, Up. The logic is, since is a 2D array, the first index is the row (y) and the second
// is the column (x). If you increase the row, you go down. If you increase the column, you go right.
int directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

/**
 * @brief Structure to represent the ship's state, including its position, resources, and map reference.
*/
typedef struct
{
    Map* mapa;
    int x;
    int y;
    int food;
    int gold;
    pid_t pid;
} Ship;

// Global variables for Signal Handlers
Ship* aux_ship = NULL;
int ship_speed = 1;
int steps_remaining = -1;
FILE* ursula_pipe = NULL;

// Functions to notify Ursula of ship events.

/**
 * @brief Sends a formatted message to Ursula whenever the ship moves, including its current position and resources.
*/
void notify_ursula_move(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, MOVE, <x>, <y>, <food>, <gold>
        fprintf(ursula_pipe, "%d, MOVE, %d, %d, %d, %d\n", s->pid, s->x, s->y, s->food, s->gold);
        fflush(ursula_pipe);
    }
}

/**
 * @brief Sends a formatted message to Ursula when the ship is initialized, including its starting position and resources.
*/
void notify_ursula_init(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, INIT, <x>, <y>, <food>, <gold>
        fprintf(ursula_pipe, "%d, INIT, %d, %d, %d, %d\n", s->pid, s->x, s->y, s->food, s->gold);
        fflush(ursula_pipe);
    }
}

/**
 * @brief Sends a formatted message to Ursula when the ship is terminating, indicating that it has finished its journey.
*/
void notify_ursula_terminate(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, TERMINATE
        fprintf(ursula_pipe, "%d, TERMINATE\n", s->pid);
        fflush(ursula_pipe);
        fclose(ursula_pipe);
        ursula_pipe = NULL;
    }
}

/**
 *  @brief Checks the current cell type on the map and updates the ship's resources accordingly.
 *  If the ship is on a BAR cell, it gains gold; if it's on a HOME cell, it gains food.
 *  It also logs these events for debugging purposes.
 * @param s Pointer to the Ship structure whose current position is being evaluated for events.
 */
void check_event(Ship* s)
{
    char cell_type = map_get_cell_type(s->mapa, s->x, s->y);
    if (cell_type == BAR)
    {
        s->gold += 10;
        fprintf(stderr, "Barco %d ha alcanzado una isla (%d, %d), oro incrementado a %d.\n", s->pid, s->x, s->y,
                s->gold);
    }
    else if (cell_type == HOME)
    {
        s->food += 20;
        fprintf(stderr, "Barco %d ha atracado con un puerto (%d, %d), comida aumentada a %d.\n", s->pid, s->x, s->y,
                s->food);
    }
}

/**
 * @brief Signal handler for SIGUSR1 to increase the ship's gold by 10 when the signal is received.
 * @param signal Signal number (unused)
 */
void sigusr1_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        aux_ship->gold += 10;
        fprintf(stderr, "Señal USR1 recibida: +10 Oro (Total: %d)\n", aux_ship->gold);
    }
}

/**
 * @brief Signal handler for SIGUSR2 to reduce resources
 * It reduces its food and gold by 10 each (not going below zero).
 * It logs the new state of the ship after the attack for debugging purposes.
 * @param signal Signal number (unused)
 */
void sigusr2_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        if (aux_ship->gold >= 10)
        {
            aux_ship->gold -= 10;
        }
        else
        {
            aux_ship->gold = 0;
        }

        if (aux_ship->food >= 10)
        {
            aux_ship->food -= 10;
        }
        else
        {
            aux_ship->food = 0;
        }
        fprintf(stderr, "Señal USR2 recibida: Reduccion de recursos! Comida: %d, Oro: %d\n",
                aux_ship->food, aux_ship->gold);
    }
}

/**
 * @brief Signal handler for SIGQUIT to perform an orderly shutdown of the ship when the signal is received.
 * It logs the final state of the ship, notifies Ursula of termination, cleans up resources,
 * and exits with the final gold as the exit code.
 * @param signal
 */
void sigquit_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        int end_gold = aux_ship->gold;
        fprintf(stderr, "Barco %d ha terminado con estado %d (SIGQUIT).\n",
                aux_ship->pid, end_gold);

        // Notify Ursula before dying
        notify_ursula_terminate(aux_ship);

        if (aux_ship->mapa) map_destroy(aux_ship->mapa);
        exit(end_gold);
    }
    exit(0);
}

/**
 * @brief Signal handler for SIGTSTP to print the current status of the ship when the signal is received.
 * It logs the ship's PID, location, food, and gold for debugging purposes.
 * @param signal Signal number (unused)
 */
void sigstp_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        printf("PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d\n",
               aux_ship->pid, aux_ship->x, aux_ship->y, aux_ship->food, aux_ship->gold);
        fflush(stdout);
    }
}

/**
 * @brief Signal handler for SIGALRM to perform random movement of the ship at regular intervals.
 * It checks if the ship has remaining steps and enough food to move, then randomly selects a direction and attempts to move.
 * If the move is successful, it updates the ship's position, reduces food, checks for events, and notifies Ursula of the move.
 * If the ship runs out of steps or food, it logs the appropriate message and may terminate if steps are exhausted.
 * @param signal Signal number (unused)
 */
void sigalrm_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        if (steps_remaining == 0)
        {
            fprintf(stderr, "Barco %d ha terminado sus pasos aleatorios.\n", aux_ship->pid);
            notify_ursula_terminate(aux_ship); // [Step 5]
            exit(aux_ship->gold);
        }

        if (aux_ship->food < 5)
        {
            fprintf(stderr, "Barco %d no tiene suficiente comida para moverse.\n", aux_ship->pid);
        }
        else
        {
            // If we use rand() % 4, we get a number between 0 and 3
            int dir_idx = rand() % 4;
            // We use the dir_idx to get the corresponding (random) direction from the directions array and calculate
            // the new position.
            int dx = directions[dir_idx][0];
            int dy = directions[dir_idx][1];
            int new_x = aux_ship->x + dx;
            int new_y = aux_ship->y + dy;

            if (map_can_sail(aux_ship->mapa, new_x, new_y))
            {
                map_remove_ship(aux_ship->mapa, aux_ship->x, aux_ship->y);
                aux_ship->x = new_x;
                aux_ship->y = new_y;
                map_set_ship(aux_ship->mapa, aux_ship->x, aux_ship->y);

                aux_ship->food -= 5;
                check_event(aux_ship);

                // Notify Ursula of random move
                notify_ursula_move(aux_ship);

                fprintf(stderr, "Barco %d en (%d, %d) con %d comida y %d oro.\n",
                        aux_ship->pid, aux_ship->x, aux_ship->y, aux_ship->food, aux_ship->gold);
            }
        }

        if (steps_remaining > 0)
        {
            steps_remaining--;
        }
        alarm(ship_speed);
    }
}

/**
 * @brief Sets up signal handlers for the ship process, including handlers for SIGUSR1, SIGUSR2, SIGQUIT, SIGTSTP, and SIGALRM.
 * Each handler is associated with its corresponding function to manage ship behavior in response to signals.
 * If any signal handler setup fails, it prints an error message and exits the process.
 */
void setup_signals()
{
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR)
    {
        perror("Error setting up SIGUSR1");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR)
    {
        perror("Error setting up SIGUSR2");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, sigquit_handler) == SIG_ERR)
    {
        perror("Error setting up SIGQUIT");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTSTP, sigstp_handler) == SIG_ERR)
    {
        perror("Error setting up SIGTSTP");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGALRM, sigalrm_handler) == SIG_ERR)
    {
        perror("Error setting up SIGALRM");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Attempts to shift the ship's position by the specified amounts in the x and y directions.
 * It checks if the ship has enough food to move and if the new position is navigable on the map.
 * If the move is successful, it updates the ship's position, reduces food, checks for events, and notifies Ursula of the move.
 * If the move is blocked or if there isn't enough food, it logs the appropriate message and responds with "NOK".
 * @param s Pointer to the Ship structure that is attempting to move.
 * @param shift_x The amount to shift in the x direction (positive for right, negative for left).
 * @param shift_y The amount to shift in the y direction (positive for down, negative for up).
 */
void shift_position(Ship* s, int shift_x, int shift_y)
{
    if (s->food < 5)
    {
        fprintf(stderr, "Barco %d sin comida suficiente.\n", s->pid);
        printf("NOK\n");
        fflush(stdout);
        return;
    }

    int new_x = s->x + shift_x;
    int new_y = s->y + shift_y;

    if (map_can_sail(s->mapa, new_x, new_y))
    {
        map_remove_ship(s->mapa, s->x, s->y);
        s->x = new_x;
        s->y = new_y;
        map_set_ship(s->mapa, s->x, s->y);
        s->food -= 5;

        check_event(s);

        // Notify Ursula of captain-commanded move
        notify_ursula_move(s);

        map_print(s->mapa); // Optional debug
        printf("OK\n");
        fflush(stdout);
        fprintf(stderr, "Barco %d en (%d, %d) con %d comida y %d oro.\n",
                s->pid, s->x, s->y, s->food, s->gold);
    }
    else
    {
        printf("NOK\n");
        fflush(stdout);
        fprintf(stderr, "Movimiento bloqueado para barco %d.\n", s->pid);
    }
}

/**
 * @brief Main loop for command mode, allowing the user to input movement commands for the ship.
 * It reads commands from standard input, processes them to move the ship accordingly, and handles the "exit" command to terminate.
 * The function also logs the ship's PID and current mode for debugging purposes.
 * @param s Pointer to the Ship structure that will be controlled through commands.
 */
void command_mode(Ship* s)
{
    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    alarm(0);

    fprintf(stderr, "Barco PID: %d. Modo captain\n", s->pid);

    while ((nread = getline(&line, &len, stdin)) != -1)
    {
        if (nread > 0 && line[nread - 1] == '\n') line[nread - 1] = '\0';

        if (strcasecmp(line, "up") == 0) shift_position(s, 0, -1);
        else if (strcasecmp(line, "down") == 0) shift_position(s, 0, 1);
        else if (strcasecmp(line, "left") == 0) shift_position(s, -1, 0);
        else if (strcasecmp(line, "right") == 0) shift_position(s, 1, 0);
        else if (strcasecmp(line, "exit") == 0)
        {
            fprintf(stderr, "Barco %d saliendo con oro %d.\n", s->pid, s->gold);
            free(line);
            notify_ursula_terminate(s);
            exit(s->gold);
        }
    }
    free(line);
}

/**
 * @brief Initializes the ship's state, including its position, resources, and map reference.
 * It also sets the ship's PID and marks its initial position on the map.
 * @param s Pointer to the Ship structure to be initialized.
 * @param mapa Pointer to the Map structure that the ship will navigate.
 * @param x Initial x-coordinate of the ship's position.
 * @param y Initial y-coordinate of the ship's position.
 * @param food Initial amount of food resources for the ship.
 */
void ship_init(Ship* s, Map* mapa, int x, int y, int food)
{
    s->mapa = mapa;
    s->x = x;
    s->y = y;
    s->food = food;
    s->gold = 0;
    s->pid = getpid();
    map_set_ship(s->mapa, s->x, s->y);
}


/**
 * @brief Parses command-line arguments to configure the ship's initial state and behavior.
 * It supports options for specifying the map file, initial position, food, random movement parameters, captain mode, and Ursula pipe.
 * The function validates the arguments and updates the corresponding variables accordingly.
 * If any argument is invalid or if required parameters are missing, it prints an error message and returns a non-zero value.
 * @param argc Argument count from the command line.
 * @param argv Argument vector from the command line.
 * @param map_file Pointer to a string that will hold the map file name (default "map.txt").
 * @param pos_x Pointer to an integer that will hold the initial x-coordinate of the ship (default 1).
 * @param pos_y Pointer to an integer that will hold the initial y-coordinate of the ship (default 1).
 * @param food Pointer to an integer that will hold the initial amount of food for the ship (default 100).
 * @param random_steps Pointer to an integer that will hold the number of random steps for random mode (default -1, meaning not set).
 * @param random_speed Pointer to an integer that will hold the speed of random movement in seconds (default 1).
 * @param use_captain Pointer to an integer that will be set to 1 if captain mode is enabled (default 0).
 * @param ursula_pipe Pointer to a string that will hold the name of the pipe for Ursula communication (default NULL).
 * @return Returns 0 on successful parsing, or a non-zero value if there was an error with the arguments.
 */
static int parse_args(int argc, char* argv[], char** map_file, int* pos_x, int* pos_y,
                      int* food, int* random_steps, int* random_speed, int* use_captain, char** ursula_pipe)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc)
        {
            *map_file = argv[++i];
        }
        else if (strcmp(argv[i], "--pos") == 0 && i + 2 < argc)
        {
            char* end;
            long v;

            // Parse X
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --pos X: %s\n", argv[i]);
                return 1;
            }
            *pos_x = (int)v;

            // Parse Y
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --pos Y: %s\n", argv[i]);
                return 1;
            }
            *pos_y = (int)v;
        }
        else if (strcmp(argv[i], "--food") == 0 && i + 1 < argc)
        {
            char* end;
            long v;

            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --food: %s\n", argv[i]);
                return 1;
            }
            *food = (int)v;
        }
        else if (strcmp(argv[i], "--random") == 0 && i + 2 < argc)
        {
            char* endptr;
            long val;

            // Parse Steps
            errno = 0;
            val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --random pasos: %s\n", argv[i]);
                return 1;
            }
            *random_steps = (int)val;

            // Parse Speed
            errno = 0;
            val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --random velocidad: %s\n", argv[i]);
                return 1;
            }
            *random_speed = (int)val;
        }
        else if (strcmp(argv[i], "--captain") == 0)
        {
            *use_captain = 1;
        }

        else if (strcmp(argv[i], "--ursula") == 0 && i + 1 < argc)
        {
            // [Step 5]
            *ursula_pipe = argv[++i];
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    char* map_file = "map.txt";
    char* ursula_fifo = NULL;
    int pos_x = 1;
    int pos_y = 1;
    int food = 100;
    int random_steps = -1;
    int random_speed = 1;
    int use_captain = 0;

    if (parse_args(argc, argv, &map_file, &pos_x, &pos_y, &food, &random_steps, &random_speed, &use_captain,
                   &ursula_fifo) != 0)
    {
        return 1;
    }

    // Connect to Ursula
    if (ursula_fifo)
    {
        ursula_pipe = fopen(ursula_fifo, "w");
        if (!ursula_pipe)
        {
            perror("Failed to open Ursula pipe in ship");
            // We continue running, just without Ursula reporting
        }
    }

    fprintf(stderr, "Mapa: %s, Posición: (%d, %d), Comida: %d\n", map_file, pos_x, pos_y, food);

    if (random_steps == -1 && !use_captain)
    {
        fprintf(stderr, "Error: Debe especificar --random N s1 o --captain\n");
        return 1;
    }
    if (use_captain && random_steps != -1)
    {
        fprintf(stderr, "Error: No puede usar --captain y --random a la vez.\n");
        return 1;
    }

    Map* mapa = map_load(map_file);
    if (!mapa)
    {
        fprintf(stderr, "Error cargando el mapa: %s\n", map_file);
        return 1;
    }

    Ship ship;
    if (map_can_sail(mapa, pos_x, pos_y))
    {
        ship_init(&ship, mapa, pos_x, pos_y, food);
    }
    else
    {
        fprintf(stderr, "Posición inicial (%d, %d) inválida.\n", pos_x, pos_y);
        map_destroy(mapa);
        exit(1);
    }

    fprintf(stderr, "Barco PID: %d\n", ship.pid);

    aux_ship = &ship;
    ship_speed = random_speed;
    steps_remaining = random_steps;

    // Notify Init
    notify_ursula_init(&ship);

    setup_signals();
    srand(time(NULL) ^ getpid());

    if (use_captain)
    {
        command_mode(&ship);
    }
    else
    {
        alarm(ship_speed);
        while (1)
        {
            pause();
        }
    }

    notify_ursula_terminate(&ship);
    map_destroy(mapa);
    return ship.gold;
}
