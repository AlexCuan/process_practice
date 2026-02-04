#include <stdio.h>

int main (int argc, char **argv)
{
    int i;

    for (i=1; i<argc; i++)
    {
        printf("argv[%i] = %s \n", i, argv[i]);
    }
    return 0;
}