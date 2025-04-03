#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "cb_hash.h"


int
main(int argc, char **argv)
{
    static const char *str1 = "test",
                      *str2 = "test2",
                      *str3 = "test3";
    cb_hash_state_t    hash_state;
    cb_hash_t          hash1,
                       hash2,
                       hash3;

    (void)argc, (void)argv;


    cb_hash_init(&hash_state);
    cb_hash_continue(&hash_state, str1, strlen(str1));
    hash1 = cb_hash_finalize(&hash_state);
    cb_hash_continue(&hash_state, str2, strlen(str2));
    hash2 = cb_hash_finalize(&hash_state);
    cb_hash_continue(&hash_state, str3, strlen(str3));
    hash3 = cb_hash_finalize(&hash_state);


    printf("hash1: %ju\n", (uint64_t)hash1);
    printf("hash2: %ju\n", (uint64_t)hash2);
    printf("hash3: %ju\n", (uint64_t)hash3);

    cb_assert(hash1 != hash2);
    cb_assert(hash2 != hash3);
    cb_assert(hash1 != hash3);

    return EXIT_SUCCESS;
}

