/*
 * @file captain.c
 * @brief Controla la lógica de la flota y administra los procesos de los barcos
 *
 * Este programa actúa como el proceso padre. Lanza barcos mediante fork/exec,
 * comunica comandos a través de tuberías (pipes) y sincroniza el estado con el
 * servidor central (Ursula).
 *
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include "map.h"

// Global pipe to Ursula
FILE* ursula_pipe = NULL;

pid_t my_pid;

/**
 * @brief Estructura para rastrear barcos lanzados y sus canales de comunicación
 */
typedef struct
{
    int id;
    pid_t pid;
    // Capitán escribe en [1], Barco lee de [0] (stdin)
    int pipe_to_ship[2];
    // Barco escribe en [1] (stdout), Capitán lee de [0]
    int pipe_from_ship[2];
    // Wrapper FILE* para pipe_from_ship[0] para el uso de getline
    FILE* read_stream;
    // Rastrear posición para detección de colisiones
    int x, y;
    // 1 si está vivo, 0 si terminó
    int active;
} ShipRecord;

ShipRecord launched_ships[100];
int ships_count = 0;

/**
 * @brief Manejador de la señal SIGCHLD para detectar cuando los barcos terminan
 * * Este manejador utiliza waitpid con WNOHANG para recolectar procesos hijos sin bloquear.
 * * Identifica qué barco terminó buscando el PID coincidente, actualiza el registro del barco,
 *   y muestra el resultado (oro recolectado o si fue hundido).
 * @param sig Número de la señal (no usado)
 */
void handle_sigchld(int sig)
{
    (void)sig;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int finished_id = -1;
        for (int i = 0; i < 100; i++)
        {
            if (launched_ships[i].pid == pid)
            {
                finished_id = launched_ships[i].id;
                launched_ships[i].pid = 0;
                launched_ships[i].active = 0;

                close(launched_ships[i].pipe_to_ship[1]);
                close(launched_ships[i].pipe_from_ship[0]);
                if (launched_ships[i].read_stream)
                {
                    fclose(launched_ships[i].read_stream);
                    launched_ships[i].read_stream = NULL;
                }
                break;
            }
        }

        if (finished_id != -1)
        {
            ships_count--;

            if (WIFEXITED(status))
            {
                int gold_collected = WEXITSTATUS(status);
                fprintf(stderr, "[Capitán] Barco %d (PID %d) ha terminado. Tesoros recolectados: %d\n",
                        finished_id, pid, gold_collected);
            }
            else if (WIFSIGNALED(status))
            {
                fprintf(stderr, "[Capitán] Barco %d (PID %d) fue hundido por la señal %d.\n",
                        finished_id, pid, WTERMSIG(status));
            }
        }
    }
}

/**
 * @brief Manejador de la señal SIGINT para iniciar el cierre ordenado de todos los barcos
 * * Cuando el capitán recibe SIGINT (ej. Ctrl+C), envía una señal SIGQUIT a todos los barcos activos,
 *   ordenándoles realizar un cierre ordenado y reportar su oro recolectado final.
 * @param sig Número de la señal (no usado)
 */
void handle_sigint(int sig)
{
    (void)sig;
    fprintf(stderr, "\n[Capitán] ¡Señal SIGINT recibida! Ordenando retirada (SIGQUIT) a todos los barcos...\n");

    for (int i = 0; i < 100; i++)
    {
        if (launched_ships[i].pid > 0 && launched_ships[i].active)
        {
            kill(launched_ships[i].pid, SIGQUIT);
        }
    }
}

/**
 * @brief Función de limpieza para notificar a Ursula de la terminación del capitán
 * * Esta función se registra con atexit para asegurar que cuando el proceso del capitán termine,
 *   envíe un mensaje final a Ursula indicando que el capitán ha finalizado.
 */
void cleanup_ursula()
{
    if (ursula_pipe)
    {
        fprintf(ursula_pipe, "%d,END_CAPT\n", my_pid);
        fflush(ursula_pipe);
        fclose(ursula_pipe);
    }
}

int main(int argc, char* argv[])
{
    my_pid = getpid();
    char* name = "Capitana Amina al-Sirafi";
    char* map_file = "map.txt";
    char* ships_file = "ships.txt";
    char* ship_path = "./ship";
    char* ursula_fifo = NULL; // Ruta al pipe de Ursula
    int random_mode = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcasecmp(argv[i], "--name") == 0)
        {
            if (i + 1 < argc) name = argv[++i];
            else
            {
                fprintf(stderr, "Error: --name requiere un valor.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcasecmp(argv[i], "--map") == 0)
        {
            if (i + 1 < argc) map_file = argv[++i];
            else
            {
                fprintf(stderr, "Error: --map requiere una ruta de archivo.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcasecmp(argv[i], "--ships") == 0)
        {
            if (i + 1 < argc) ships_file = argv[++i];
            else
            {
                fprintf(stderr, "Error: --ships requiere una ruta de archivo.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--ship-path") == 0)
        {
            if (i + 1 < argc) ship_path = argv[++i];
            else
            {
                fprintf(stderr, "Error: --ship-path requiere una ruta.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcasecmp(argv[i], "--random") == 0)
        {
            random_mode = 1;
        }
        else if (strcasecmp(argv[i], "--ursula") == 0 && i + 1 < argc) ursula_fifo = argv[++i]; // Parsear arg Ursula

    }


    // Manejar pipes rotos. Cuando el proceso de un barco muere, escribir en su pipe causará SIGPIPE.
    // Queremos ignorarlo y manejarlo con gracia.

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("Error configurando SIGPIPE");
        return EXIT_FAILURE;
    }


    if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        perror("Error configurando SIGINT");
        return EXIT_FAILURE;
    }
    if (signal(SIGCHLD, handle_sigchld) == SIG_ERR) {
        perror("Error configurando SIGCHLD");
        return EXIT_FAILURE;
    }


    // Inicializar array de barcos
    for (int i = 0; i < 100; i++)
    {
        launched_ships[i].pid = 0;
        launched_ships[i].active = 0;
        launched_ships[i].read_stream = NULL;
    }

    fprintf(stderr, "Nombre del Capitán: %s PID: %d\n", name, my_pid);

    // Conectar a Ursula si se solicitó
    if (ursula_fifo)
    {
        ursula_pipe = fopen(ursula_fifo, "w");
        if (ursula_pipe)
        {
            // fprintf escribe el mensaje en el pipe directamente
            fprintf(ursula_pipe, "%d,INIT_CAPT\n", my_pid);
            // fflush: Fuerza a tomar lo que está actualmente en el buffer y escribirlo inmediatamente
            // en el pipe.
            fflush(ursula_pipe);
            // Registrar la limpieza para enviar FIN_CAPT a la salida
            atexit(cleanup_ursula);
        }
        else
        {
            perror("Fallo al abrir la tubería hacia Ursula");
        }
    }

    Map* map = map_load(map_file);
    if (!map)
    {
        fprintf(stderr, "Error cargando el mapa %s\n", map_file);
        return EXIT_FAILURE;
    }

    // Cargar información de Barcos
    FILE* file = fopen(ships_file, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error abriendo el archivo de barcos: %s\n", ships_file);
        return EXIT_FAILURE;
    }

    int id, x, y, speed;
    char* line = NULL;
    size_t len = 0;
    // Usamos ssize_t porque los retornos de getline pueden ser -1 en caso de error o EOF.
    // Usar ssize_t nos permite comprobar correctamente estas condiciones.
    ssize_t read_len;


    // getline para leer y sscanf para parsear
    while ((read_len = getline(&line, &len, file)) != -1)
    {
        if (read_len <= 1) continue;

        if (sscanf(line, "%d (%d,%d) %d", &id, &x, &y, &speed) == 4)
        {
            fprintf(stderr, "Lanzando Barco ID: %d, Posición: (%d, %d)\n", id, x, y);

            // Crear pipes
            int p_to_s[2]; // Escribir al barco
            int p_from_s[2]; // Leer del barco
            if (pipe(p_to_s) == -1 || pipe(p_from_s) == -1)
            {
                perror("fallo en pipe");
                continue;
            }

            pid_t pid = fork();
            if (pid < 0)
            {
                perror("fallo en fork");
                continue;
            }

            if (pid == 0) // Proceso Hijo
            {
                // Redirigir STDIN/STDOUT a los pipes
                dup2(p_to_s[0], STDIN_FILENO); // Leer del padre
                dup2(p_from_s[1], STDOUT_FILENO); // Escribir al padre

                // Cerramos extremos no usados de los pipes
                close(p_to_s[0]);
                close(p_to_s[1]);
                close(p_from_s[0]);
                close(p_from_s[1]);

                // Cerrar pipes que pertenecen a barcos PREVIAMENTE lanzados.
                // Cuando fork() se ejecuta, este hijo hereda TODOS los descriptores de archivo abiertos
                // del padre, incluyendo pipes conectados al Barco 1, Barco 2, etc.
                // Debemos cerrar estos pipes aquí. Si no lo hacemos,
                // los barcos anteriores nunca recibirán señales de EOF (terminación)
                // porque este nuevo hijo está manteniendo accidentalmente sus pipes abiertos.
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        close(launched_ships[i].pipe_to_ship[1]);
                        close(launched_ships[i].pipe_from_ship[0]);
                    }
                }

                // Cerrar pipe de Ursula en el hijo (el hijo abrirá su propia conexión)
                if (ursula_pipe) fclose(ursula_pipe);

                // Señales por defecto
                signal(SIGINT, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);

                char x_str[12], y_str[12], speed_str[12];
                // Convertir enteros a strings para los argumentos de exec
                snprintf(x_str, sizeof(x_str), "%d", x);
                snprintf(y_str, sizeof(y_str), "%d", y);
                snprintf(speed_str, sizeof(speed_str), "%d", speed);

                // Propagar el arg --ursula al barco
                if (random_mode)
                {
                    if (ursula_fifo)
                        execl(ship_path, "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file, "--ursula", ursula_fifo, NULL);
                    else
                        execl(ship_path, "ship", "--pos", x_str, y_str, "--random", "10", speed_str, "--map", map_file, NULL);
                }
                else
                {
                    if (ursula_fifo)
                        execl(ship_path, "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, "--ursula", ursula_fifo, NULL);
                    else
                        execl(ship_path, "ship", "--pos", x_str, y_str, "--captain", "--map", map_file, NULL);
                }

                perror("fallo en execl");
                exit(EXIT_FAILURE);
            }
            else // Proceso Padre
            {
                // Cerrar extremo de lectura del pipe de escritura
                close(p_to_s[0]);
                // Cerrar extremo de escritura del pipe de lectura
                close(p_from_s[1]);

                int placed = 0;
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].pid == 0)
                    {
                        launched_ships[i].id = id;
                        launched_ships[i].pid = pid;
                        launched_ships[i].x = x;
                        launched_ships[i].y = y;
                        launched_ships[i].pipe_to_ship[1] = p_to_s[1];
                        launched_ships[i].pipe_from_ship[0] = p_from_s[0];
                        launched_ships[i].read_stream = fdopen(p_from_s[0], "r");
                        launched_ships[i].active = 1;
                        placed = 1;
                        break;
                    }
                }
                if (placed) ships_count++;
            }
        }
    }

    free(line);
    fclose(file);

    if (random_mode)
    {
        fprintf(stderr, "[Capitán] Esperando a que los barcos terminen (Modo Aleatorio)...\n");
        while (ships_count > 0)
        {
            pause();
        }
    }
    else
    {
        // Buffers para el uso de getline
        char* cmd_line = NULL;
        size_t cmd_len = 0;
        char* resp_line = NULL;
        size_t resp_len = 0;

        while (ships_count > 0)
        {
            // Prompt to stderr
            fprintf(stderr, "Introduce command [exit | status | <id> up/down/right/left]: ");

            if (getline(&cmd_line, &cmd_len, stdin) == -1)
            {
                if (errno == EINTR) {
                    clearerr(stdin);
                    continue; // Un barco murió, el bucle reevaluará ships_count > 0
                }
            }

            cmd_line[strcspn(cmd_line, "\n")] = 0;
            if (strlen(cmd_line) == 0) continue;

            if (strcasecmp(cmd_line, "exit") == 0)
            {
                fprintf(stderr, "Saliendo y terminando todos los barcos.\n");
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        kill(launched_ships[i].pid, SIGQUIT);
                    }
                }
                while (ships_count > 0)
                {
                    pause();
                }
                break;
            }
            else if (strcasecmp(cmd_line, "status") == 0)
            {
                for (int i = 0; i < 100; i++)
                {
                    if (launched_ships[i].active)
                    {
                        kill(launched_ships[i].pid, SIGTSTP);

                        ssize_t n = getline(&resp_line, &resp_len, launched_ships[i].read_stream);

                        if (n > 0)
                        {
                            // Parsing status response from Ship
                            int s_pid, s_x, s_y, s_food, s_gold;
                            if (sscanf(resp_line, "PID de barco: %d, Ubicación: (%d, %d), Comida: %d, Oro: %d",
                                       &s_pid, &s_x, &s_y, &s_food, &s_gold) == 5)
                            {
                                fprintf(stderr, "Barco %d vivo (PID: %d) Ubicación: (%d, %d) Comida: %d Oro: %d\n",
                                        launched_ships[i].id, s_pid, s_x, s_y, s_food, s_gold);
                            }
                            else
                            {
                                fprintf(stderr, "Estado del Barco %d: %s", launched_ships[i].id, resp_line);
                            }
                        }
                    }
                }
                fprintf(stderr, "Número de barcos vivos: %d\n", ships_count);
            }
            else
            {
                // Parseando el Comando del Usuario
                int target_id;
                char action[32];
                if (sscanf(cmd_line, "%d %31s", &target_id, action) == 2)
                {
                    int found_idx = -1;
                    for (int i = 0; i < 100; i++)
                    {
                        if (launched_ships[i].active && launched_ships[i].id == target_id)
                        {
                            found_idx = i;
                            break;
                        }
                    }

                    if (found_idx != -1)
                    {
                        if (strcasecmp(action, "exit") == 0)
                        {
                            fprintf(stderr, "Enviando acción de salir al barco %d...\n", target_id);
                            // La D de dprintf significa que escribe directamente en el descriptor.
                            // Esto está destinado a pipes anónimos, no se puede usar en Ursula ya que Ursula usa FIFOs
                            // (pipes con nombre)
                            dprintf(launched_ships[found_idx].pipe_to_ship[1], "exit\n");
                        }
                        else if (strcasecmp(action, "up") == 0 || strcasecmp(action, "down") == 0 ||
                            strcasecmp(action, "left") == 0 || strcasecmp(action, "right") == 0)
                        {
                            int dx = 0, dy = 0;
                            if (strcasecmp(action, "up") == 0) dy = -1;
                            if (strcasecmp(action, "down") == 0) dy = 1;
                            if (strcasecmp(action, "left") == 0) dx = -1;
                            if (strcasecmp(action, "right") == 0) dx = 1;

                            int new_x = launched_ships[found_idx].x + dx;
                            int new_y = launched_ships[found_idx].y + dy;

                            // Comprobar colisión con otros barcos
                            int collision = 0;
                            for (int i = 0; i < 100; i++)
                            {
                                if (launched_ships[i].active && launched_ships[i].id != target_id)
                                {
                                    if (launched_ships[i].x == new_x && launched_ships[i].y == new_y)
                                    {
                                        collision = 1;
                                        break;
                                    }
                                }
                            }

                            if (collision)
                            {
                                fprintf(stderr, "No se puede realizar el movimiento hacia %s para el barco %d (colisión).\n",
                                        action, target_id);
                            }
                            else
                            {
                                // Comprobar límites del mapa/rocas
                                if (!map_can_sail(map, new_x, new_y))
                                {
                                    fprintf(stderr, "No se puede mover hacia %s: El destino está bloqueado/roca.\n", action);
                                }
                                else
                                {
                                    // Enviar Comando
                                    dprintf(launched_ships[found_idx].pipe_to_ship[1], "%s\n", action);

                                    // Esperar confirmación OK/NOK usando getline
                                    ssize_t n = getline(&resp_line, &resp_len, launched_ships[found_idx].read_stream);

                                    if (n > 0)
                                    {
                                        // Recortar salto de línea
                                        resp_line[strcspn(resp_line, "\n")] = 0;
                                        if (strcmp(resp_line, "OK") == 0)
                                        {
                                            // Actualizar posición SOLO si es confirmado
                                            launched_ships[found_idx].x = new_x;
                                            launched_ships[found_idx].y = new_y;
                                            fprintf(stderr, "Barco %d movido hacia %s a (%d, %d)\n", target_id, action, new_x,
                                                    new_y);
                                        }
                                        else
                                        {
                                            fprintf(stderr, "Barco %d rechazó el movimiento\n",
                                                    target_id);
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            fprintf(stderr, "Comando desconocido: %s\n", action);
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Barco %d no encontrado o no está vivo.\n", target_id);
                    }
                    fprintf(stderr, "Número de barcos vivos: %d\n", ships_count);
                }
            }
        }
        if (cmd_line) free(cmd_line);
        if (resp_line) free(resp_line);
    }

    fprintf(stderr, "[Capitán] Esperando a que los barcos terminen...\n");
    while (ships_count > 0)
    {
        pause();
    }

    fprintf(stderr, "[Capitán] Todos los barcos han regresado. Terminando ejecución.\n");
    map_destroy(map);
    return EXIT_SUCCESS;
}