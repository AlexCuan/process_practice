#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
    pid_t pid_fin;
    int status;
    pid_t pid = fork();
    if (pid < 0){ /* pid < 0 means error */
        fprintf(stderr,"Fork Failed\n");
        exit(EXIT_FAILURE);
    } else if (pid == 0){ /* child process */
        printf("I am a child with PID [%i] \n",getpid());
        argv[0] = "./imprimo";
        execvp("./imprimo",argv);


        printf("ERROR!!! I am %d\n",getpid());

        ///
        fprintf(stderr,"Exec Failed\n");
        _exit(EXIT_FAILURE);
    } else{ /* Parent process */
        /* Parent will wait for the child to complete */
        pid_fin = waitpid(-1,&status, 0);
        if (pid_fin == -1)
            perror(strerror(errno));
        else{
            printf("Child %d Completed\n", pid_fin);
            printf("with status: %d\n", WEXITSTATUS(status));
        }
    }
    exit(EXIT_SUCCESS);
}