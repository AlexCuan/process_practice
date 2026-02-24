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

// Direcciones: Derecha, Abajo, Izquierda, Arriba. La lógica es, dado que es un array 2D, el primer índice es la fila (y) y el segundo
// es la columna (x). Si incrementas la fila, vas hacia abajo. Si incrementas la columna, vas a la derecha.
int directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};

/**
 * @brief Estructura para representar el estado del barco, incluyendo su posición, recursos y referencia al mapa.
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

// Variables globales para los Manejadores de Señales
Ship* aux_ship = NULL;
int ship_speed = 1;
int steps_remaining = -1;
FILE* ursula_pipe = NULL;

// Funciones para notificar a Ursula los eventos del barco.

/**
 * @brief Envía un mensaje formateado a Ursula cada vez que el barco se mueve, incluyendo su posición actual y recursos.
*/
void notify_ursula_move(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, MOVE, <x>, <y>, <food>, <gold>
        fprintf(ursula_pipe, "%d,MOVE,%d,%d,%d,%d\n", s->pid, s->x, s->y, s->food, s->gold);
        fflush(ursula_pipe);
    }
}

/**
 * @brief Envía un mensaje formateado a Ursula cuando el barco es inicializado, incluyendo su posición inicial y recursos.
*/
void notify_ursula_init(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, INIT, <x>, <y>, <food>, <gold>
        fprintf(ursula_pipe, "%d,INIT,%d,%d,%d,%d\n", s->pid, s->x, s->y, s->food, s->gold);
        fflush(ursula_pipe);
    }
}

/**
 * @brief Envía un mensaje formateado a Ursula cuando el barco va a terminar, indicando que ha finalizado su viaje.
*/
void notify_ursula_terminate(Ship* s)
{
    if (ursula_pipe)
    {
        // Format: <PID>, TERMINATE
        fprintf(ursula_pipe, "%d,TERMINATE\n", s->pid);
        fflush(ursula_pipe);
        fclose(ursula_pipe);
        ursula_pipe = NULL;
    }
}

/**
 *  @brief Comprueba el tipo de celda actual en el mapa y actualiza los recursos del barco en consecuencia.
 *  Si el barco está en una celda BAR, gana oro; si está en una celda HOME, gana comida.
 *  También registra estos eventos en la consola para propósitos de depuración.
 * @param s Puntero a la estructura Ship cuya posición actual está siendo evaluada.
 */
void check_event(Ship* s)
{
    char cell_type = map_get_cell_type(s->mapa, s->x, s->y);
    if (cell_type == BAR)
    {
        fprintf(stderr, "Barco %d ha alcanzado una isla (%d, %d), oro incrementado a %d.\n", s->pid, s->x, s->y,
                s->gold);
    }
    else if (cell_type == HOME)
    {
        fprintf(stderr, "Barco %d ha atracado con un puerto (%d, %d), comida aumentada a %d.\n", s->pid, s->x, s->y,
                s->food);
    }
}

/**
 * @brief Manejador de señal para SIGUSR1 para incrementar el oro del barco en 10 cuando se recibe la señal.
 * @param signal Número de señal (no usado)
 */
void sigusr1_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        aux_ship->gold += 10;
        fprintf(stderr, "Barco %d: Señal USR1 recibida (+10 Oro). Oro Total: %d\n",
                aux_ship->pid, aux_ship->gold);
    }
}

/**
 * @brief Manejador de señal para SIGUSR2 para reducir recursos
 * Reduce su comida y oro en 10 cada uno (sin bajar de cero).
 * Registra el nuevo estado del barco después del ataque para propósitos de depuración.
 * @param signal Número de señal (no usado)
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
        fprintf(stderr, "Barco %d: Señal USR2 recibida (¡Ataque!). Comida restante: %d, Oro restante: %d\n",
                aux_ship->pid, aux_ship->food, aux_ship->gold);
    }
}

/**
 * @brief Manejador de señal para SIGQUIT para realizar un cierre ordenado del barco cuando se recibe la señal.
 * Registra el estado final del barco, notifica a Ursula de la terminación, limpia recursos,
 * y sale con el oro final como código de salida.
 * @param signal Número de señal (no usado)
 */
void sigquit_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        int end_gold = aux_ship->gold;
        fprintf(stderr, "Barco %d ha terminado con estado %d (SIGQUIT).\n",
                aux_ship->pid, end_gold);

        // Notificar a Ursula antes de morir
        notify_ursula_terminate(aux_ship);

        if (aux_ship->mapa) map_destroy(aux_ship->mapa);
        exit(end_gold);
    }
    exit(EXIT_SUCCESS);
}

/**
 * @brief Manejador de señal para SIGTSTP para imprimir el estado actual del barco cuando se recibe la señal.
 * Registra el PID del barco, ubicación, comida y oro para propósitos de depuración.
 * @param signal Número de señal (no usado)
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
 * @brief Manejador de señal para SIGALRM para realizar el movimiento aleatorio del barco a intervalos regulares.
 * Comprueba si el barco tiene pasos restantes y suficiente comida para moverse, luego selecciona aleatoriamente una dirección e intenta moverse.
 * Si el movimiento es exitoso, actualiza la posición del barco, reduce la comida, comprueba eventos, y notifica a Ursula del movimiento.
 * Si el barco se queda sin pasos o comida, registra el mensaje apropiado y puede terminar si los pasos se agotan.
 * @param signal Número de señal (no usado)
 */
void sigalrm_handler(int signal)
{
    (void)signal;
    if (aux_ship != NULL)
    {
        if (steps_remaining == 0)
        {
            fprintf(stderr, "Barco %d ha terminado sus pasos aleatorios.\n", aux_ship->pid);
            notify_ursula_terminate(aux_ship);
            exit(aux_ship->gold);
        }

        if (aux_ship->food < 5)
        {
            fprintf(stderr, "Barco %d no tiene suficiente comida para moverse.\n", aux_ship->pid);
        }
        else
        {
            // Si usamos rand() % 4, obtenemos un número entre 0 y 3
            int dir_idx = rand() % 4;
            // Usamos dir_idx para obtener la dirección (aleatoria) correspondiente del array directions y calcular
            // la nueva posición.
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

                // Notificar a Ursula del movimiento aleatorio
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
 * @brief Configura los manejadores de señales para el proceso del barco, incluyendo manejadores para SIGUSR1, SIGUSR2, SIGQUIT, SIGTSTP y SIGALRM.
 * Cada manejador está asociado con su función correspondiente para manejar el comportamiento del barco en respuesta a las señales.
 * Si la configuración de algún manejador de señal falla, imprime un mensaje de error y el proceso termina.
 */
void setup_signals()
{
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR)
    {
        perror("Error configurando SIGUSR1");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGUSR2, sigusr2_handler) == SIG_ERR)
    {
        perror("Error configurando SIGUSR2");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, sigquit_handler) == SIG_ERR)
    {
        perror("Error configurando SIGQUIT");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTSTP, sigstp_handler) == SIG_ERR)
    {
        perror("Error configurando SIGTSTP");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGALRM, sigalrm_handler) == SIG_ERR)
    {
        perror("Error configurando SIGALRM");
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Intenta desplazar la posición del barco por las cantidades especificadas en las direcciones x e y.
 * Comprueba si el barco tiene suficiente comida para moverse y si la nueva posición es navegable en el mapa.
 * Si el movimiento es exitoso, actualiza la posición del barco, reduce la comida, comprueba eventos, y notifica a Ursula del movimiento.
 * Si el movimiento está bloqueado o si no hay suficiente comida, registra el mensaje apropiado y responde con "NOK".
 * @param s Puntero a la estructura Ship que está intentando moverse.
 * @param shift_x La cantidad a desplazar en la dirección x (positivo para derecha, negativo para izquierda).
 * @param shift_y La cantidad a desplazar en la dirección y (positivo para abajo, negativo para arriba).
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

        // Notificar a Ursula del movimiento ordenado por el capitán
        notify_ursula_move(s);

        map_print(s->mapa); // Opcional para depuración
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
 * @brief Bucle principal para el modo comando, permitiendo al usuario introducir comandos de movimiento para el barco.
 * Lee comandos de la entrada estándar, los procesa para mover el barco correspondientemente, y maneja el comando "salir" para terminar.
 * La función también registra el PID del barco y el modo actual para propósitos de depuración.
 * @param s Puntero a la estructura Ship que será controlada a través de comandos.
 */
void command_mode(Ship* s)
{
    char* line = NULL;
    size_t len = 0;
    ssize_t nread;

    alarm(0);

    fprintf(stderr, "Barco PID: %d. Modo capitán\n", s->pid);

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
 * @brief Inicializa el estado del barco, incluyendo su posición, recursos y referencia al mapa.
 * También establece el PID del barco y marca su posición inicial en el mapa.
 * @param s Puntero a la estructura Ship a ser inicializada.
 * @param mapa Puntero a la estructura Map por la que navegará el barco.
 * @param x Coordenada x inicial de la posición del barco.
 * @param y Coordenada y inicial de la posición del barco.
 * @param food Cantidad inicial de recursos de comida para el barco.
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
 * @brief Analiza los argumentos de línea de comandos para configurar el estado inicial y el comportamiento del barco.
 * Soporta opciones para especificar el archivo del mapa, posición inicial, comida, parámetros de movimiento aleatorio, modo capitán y pipe de Ursula.
 * La función valida los argumentos y actualiza las variables correspondientes en consecuencia.
 * Si algún argumento es inválido o si faltan parámetros requeridos, imprime un mensaje de error y devuelve un valor distinto de cero.
 * @param argc Cantidad de argumentos de la línea de comandos.
 * @param argv Vector de argumentos de la línea de comandos.
 * @param map_file Puntero a un string que contendrá el nombre del archivo del mapa (por defecto "map.txt").
 * @param pos_x Puntero a un entero que contendrá la coordenada x inicial del barco (por defecto 1).
 * @param pos_y Puntero a un entero que contendrá la coordenada y inicial del barco (por defecto 1).
 * @param food Puntero a un entero que contendrá la cantidad inicial de comida para el barco (por defecto 100).
 * @param random_steps Puntero a un entero que contendrá el número de pasos aleatorios para el modo aleatorio (por defecto -1, no establecido).
 * @param random_speed Puntero a un entero que contendrá la velocidad del movimiento aleatorio en segundos (por defecto 1).
 * @param use_captain Puntero a un entero que se establecerá a 1 si el modo capitán está habilitado (por defecto 0).
 * @param ursula_pipe Puntero a un string que contendrá el nombre de la tubería para la comunicación con Ursula (por defecto NULL).
 * @return Devuelve 0 en caso de análisis exitoso, o un valor distinto de cero si hubo un error con los argumentos.
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
                return EXIT_FAILURE;
            }
            *pos_x = (int)v;

            // Parse Y
            errno = 0;
            v = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --pos Y: %s\n", argv[i]);
                return EXIT_FAILURE;;
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
                return EXIT_FAILURE;;
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
                return EXIT_FAILURE;;
            }
            *random_steps = (int)val;

            // Parse Speed
            errno = 0;
            val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX)
            {
                fprintf(stderr, "Valor inválido para --random velocidad: %s\n", argv[i]);
                return EXIT_FAILURE;;
            }
            *random_speed = (int)val;
        }
        else if (strcmp(argv[i], "--captain") == 0)
        {
            *use_captain = 1;
        }

        else if (strcmp(argv[i], "--ursula") == 0 && i + 1 < argc)
        {
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
        return EXIT_FAILURE;
    }

    // Conectar a Ursula
    if (ursula_fifo)
    {
        ursula_pipe = fopen(ursula_fifo, "w");
        if (!ursula_pipe)
        {
            perror("Fallo al abrir la tubería de Ursula en el barco");
            // Continuamos la ejecución, pero sin reportar a Ursula
        }
    }

    fprintf(stderr, "Mapa: %s, Posición: (%d, %d), Comida: %d\n", map_file, pos_x, pos_y, food);

    if (random_steps == -1 && !use_captain)
    {
        fprintf(stderr, "Error: Debe especificar --random N s1 o --captain\n");
        return EXIT_FAILURE;;
    }
    if (use_captain && random_steps != -1)
    {
        fprintf(stderr, "Error: No puede usar --captain y --random a la vez.\n");
        return EXIT_FAILURE;;
    }

    Map* mapa = map_load(map_file);
    if (!mapa)
    {
        fprintf(stderr, "Error cargando el mapa: %s\n", map_file);
        return EXIT_FAILURE;;
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
        exit(EXIT_FAILURE);
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
