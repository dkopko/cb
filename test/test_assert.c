#include "cb_assert.h"

int
main(int argc, char **argv)
{
    (void)argc, (void)argv;
    cb_assert(false);
    return 0;
}

