#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
    uint64_t a, b, c, d;
    int a_result, b_result, c_result, d_result;
    (void)argc, (void)argv;

    a = strtoull("0xFFFFFFFFFFFFFFFF", NULL, 16);
    b = UINT64_MAX;
    c = strtoull("0x0000000000000000", NULL, 16);
    d = 0;

    a_result = __builtin_ctzll(a);
    b_result = __builtin_ctzll(b);
    c_result = __builtin_ctzll(c);
    d_result = __builtin_ctzll(d);

    printf("a == b ? %d\n", a == b);
    printf("c == d ? %d\n", c == d);
    printf("sizeof(unsigned int): %zu\n", sizeof(unsigned int));
    printf("sizeof(unsigned long): %zu\n", sizeof(unsigned long));
    printf("sizeof(unsigned long long): %zu\n", sizeof(unsigned long long));
    printf("sizeof(uint64_t): %zu\n", sizeof(uint64_t));

    printf("dynamic  __builtin_ctzll(a: 0x%016jx): %d\n", (uintmax_t)a, a_result);
    printf("constant __builtin_ctzll(b: 0x%016jx): %d\n", (uintmax_t)b, b_result);
    printf("dynamic  __builtin_ctzll(c: 0x%016jx): %d\n", (uintmax_t)c, c_result);
    printf("constant __builtin_ctzll(d: 0x%016jx): %d\n", (uintmax_t)d, d_result);

    assert(a == UINT64_MAX);
    assert(c == 0);

    return 0;
}
