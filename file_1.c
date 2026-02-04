#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main ()
{
    pid_t pid, pid_fin;
    pid = 0;
    int status;
    printf("[%i] Soy tu padre y mi PID es: [%i] \n", getpid(), getpid());
    const char * phrases[3]= {"Hola", "Soy Iñigo Montoya", "Tu mataste a mi padre, preparate a morir"};
    for (int i=0; i<3;i++)
    {
        pid = fork();
        if (pid<0)
        {
            fprintf(stderr, "Fork failed\n");
            exit(EXIT_FAILURE);
        } else if (pid == 0)
        {
            printf("[%i] %s\n", getpid(), phrases[i]);
            _exit(i+1);
        }
        else
        {
            pid_fin = wait(&status);
            if (pid_fin== -1)
            {
                perror(strerror(errno));
            }
            else
            {
                printf("Child %d Completed\n", pid_fin);
                printf("with status: %d\n", WEXITSTATUS(status));
                printf("Hola mundo");
            }
        }
    }
    exit(EXIT_SUCCESS);
}