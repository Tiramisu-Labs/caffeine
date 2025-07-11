#include <stdio.h>
#include <stdlib.h>
#include "../include/server.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(2, "Error: wrong number of argument\n");
        return 1;
    }

    server_run(atoi(argv[1]));
}