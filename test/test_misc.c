#include "cb_bits.h"
#include "cb_misc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Returns the lowest power of 2 size_t which is greater than 'x'.
 */
/*FIXME improve implementation. */
CB_INLINE size_t
power_of_2_gt_size_simple(size_t x)
{
    size_t result = 0;
    while (x) { result |= x; x >>= 1; }
    result += 1;
//assert(is_power_of_2_size(result));
    return result;
}

/*
 * For a size_t 'x' which is a power of two, returns floor(log2(x)).
 */
/*FIXME improve implementation. */
CB_INLINE unsigned int
log2_of_power_of_2_size_simple(size_t x)
{
    assert(is_power_of_2_size(x));
    unsigned int result = 0;
    while (x != 1) { x >>= 1; result++; }
    return result;
}


static void
test_mask_below_bit(void)
{
    for (uint8_t i = 0; i < UINT8_MAX; ++i)
    {
        printf("mask_below_bit(%ju): 0x%016jx\n",
               (uintmax_t)i, mask_below_bit(i));
    }
}


static void
wtf(void)
{
    size_t arr[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,
        SIZE_MAX/2 - 1, SIZE_MAX/2, SIZE_MAX/2 + 1,
        SIZE_MAX - 2, SIZE_MAX - 1, SIZE_MAX};

    printf("Begin wtf\n");
    for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i)
    {
        size_t result = 0;
        size_t x = arr[i];
        size_t leading_zeros  = clz64(arr[i]);
        size_t mask_off = 64 - leading_zeros;
        size_t mask = mask_below_bit(mask_off);

        while (x) { result |= x; x >>= 1; }

        printf("result: 0x%016jx\n", (uintmax_t)result);
        printf("mask:   0x%016jx (leading_zeros: %zu, mask_off: %zu)\n",
               (uintmax_t)mask, leading_zeros, mask_off);

        printf("result1: 0x%016jx\n", (uintmax_t)result + 1);
        printf("mask1:   0x%016jx\n", (uintmax_t)mask + 1);

        printf("\n");
    }

    for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i)
    {
        size_t v0 = power_of_2_gt_size_simple(arr[i]);
        size_t v1 = power_of_2_gt_size(arr[i]);

        printf("v0: 0x%016jx\n", (uintmax_t)v0);
        printf("v1: 0x%016jx\n", (uintmax_t)v1);
        printf("\n");
    }

    printf("End wtf\n");
}


static void
wtf2(void)
{
    printf("Begin wtf2\n");
    for (uint8_t i = 0; i < 63; ++i)
    {
        size_t v0 = log2_of_power_of_2_size_simple((size_t)1 << i);
        size_t v1 = log2_of_power_of_2_size((size_t)1 << i);

        printf("v0: 0x%016jx\n", (uintmax_t)v0);
        printf("v1: 0x%016jx\n", (uintmax_t)v1);
        printf("\n");
    }
    printf("End wtf2\n");
}


static void
test_power_of_2_gt_size_old(void)
{
    printf("DANDEBUG0\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)0, power_of_2_gt_size(0));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)0, power_of_2_gt_size_simple(0));

    printf("DANDEBUG1\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)1, power_of_2_gt_size(1));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)1, power_of_2_gt_size_simple(1));

    printf("DANDEBUG2\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)3, power_of_2_gt_size(3));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)3, power_of_2_gt_size_simple(3));

    printf("DANDEBUG3\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)SIZE_MAX/2, power_of_2_gt_size(SIZE_MAX/2));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)SIZE_MAX/2, power_of_2_gt_size_simple(SIZE_MAX/2));

    printf("DANDEBUG4\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)SIZE_MAX - 5, power_of_2_gt_size(SIZE_MAX - 5));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)SIZE_MAX - 5, power_of_2_gt_size_simple(SIZE_MAX - 5));

    printf("DANDEBUG5\n");
    printf("power_of_2_gt_size(%zu): %zu\n", (size_t)SIZE_MAX - 1, power_of_2_gt_size(SIZE_MAX - 1));
    printf("power_of_2_gt_size_simple(%zu): %zu\n", (size_t)SIZE_MAX - 1, power_of_2_gt_size_simple(SIZE_MAX - 1));
}


static void
test_is_power_of_2(void)
{
    for (uintmax_t i = 0; i < 33; ++i)
        printf("is_power_of_2(%ju): %d\n", i, is_power_of_2(i));

    for (uintmax_t i = UINTMAX_MAX - 8; i != 0; ++i)
        printf("is_power_of_2(%ju): %d\n", i, is_power_of_2(i));
}


static void
test_power_of_2_gt(void)
{
    for (uintmax_t i = 0; i < 33; ++i)
        printf("power_of_2_gt(%ju): %ju\n", i, power_of_2_gt(i));

    for (uintmax_t i = UINTMAX_MAX - 8; i != 0; ++i)
        printf("power_of_2_gt(%ju): %ju\n", i, power_of_2_gt(i));
}


static void
test_power_of_2_gte(void)
{
    for (uintmax_t i = 0; i < 33; ++i)
        printf("power_of_2_gte(%ju): %ju\n", i, power_of_2_gte(i));

    for (uintmax_t i = UINTMAX_MAX - 8; i != 0; ++i)
        printf("power_of_2_gte(%ju): %ju\n", i, power_of_2_gte(i));
}

int
main(int argc, char **argv)
{
    (void)argc, (void)argv;

    test_mask_below_bit();
    test_power_of_2_gt_size_old();
    test_is_power_of_2();
    test_power_of_2_gt();
    test_power_of_2_gte();
    //wtf();
    //wtf2();

    return EXIT_SUCCESS;
}
