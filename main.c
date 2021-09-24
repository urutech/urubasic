#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include "stdintw.h"
#include "urubasic.h"

#ifdef _MSC_VER
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static int read_from_fileno(void *arg)
{
    // read one character from file
    long fileno = (long) arg;
    char buffer[4];
    int rc = read(fileno, buffer, 1);
    if (rc)
        return buffer[0];

    return 0;
}

static int fct_rnd(int n, struct urubasic_type *arg, void *user)
{
    int res = rand();
    if (n > 1 && urubasic_is_number(&arg[1]))
        res %= urubasic_get_number(&arg[1]);
    else
        res %= 0x80000000;
    urubasic_set_number(&arg[0], res);
    return res;
}

static int fct_randomize(int n, struct urubasic_type *arg, void *user)
{
    unsigned int seed = time(NULL);

#ifdef _MSC_VER
    seed *= GetTickCount();
#else
    seed *= getpid();
#endif
    srand(seed);
    rand();
    return 0;
}

int main(int argc, char *argv[])
{
    int fileno = 0;
    if (argc > 1)
        fileno = open(argv[1], 0);

    urubasic_init(4096 /* max_mem */, 128 /* max_symbols */, read_from_fileno, (void *) (long) fileno);
    urubasic_add_function("RND", fct_rnd, NULL);
    urubasic_add_function("RANDOMIZE", fct_randomize, NULL);
    urubasic_execute(0);
    urubasic_term();
    return 0;
}
