#include "cb_bits.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Returns the lowest power of 2 size_t which is greater than 'x'.
 */
/*FIXME improve implementation. */
CB_INLINE size_t
power_of_2_size_gt_simple(size_t x)
{
    size_t result = 0;
    while (x) { result |= x; x >>= 1; }
    result += 1;
    assert(is_power_of_2_size(result));
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
        size_t v0 = power_of_2_size_gt_simple(arr[i]);
        size_t v1 = power_of_2_size_gt(arr[i]);

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
test_power_of_2_size_gt()
{
    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)0, power_of_2_size_gt(0));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)0, power_of_2_size_gt_simple(0));

    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)1, power_of_2_size_gt(1));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)1, power_of_2_size_gt_simple(1));

    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)3, power_of_2_size_gt(3));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)3, power_of_2_size_gt_simple(3));

    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)SIZE_MAX/2, power_of_2_size_gt(SIZE_MAX/2));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)SIZE_MAX/2, power_of_2_size_gt_simple(SIZE_MAX/2));

    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)SIZE_MAX - 5, power_of_2_size_gt(SIZE_MAX - 5));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)SIZE_MAX - 5, power_of_2_size_gt_simple(SIZE_MAX - 5));

    printf("power_of_2_size_gt(%zu): %zu\n", (size_t)SIZE_MAX - 1, power_of_2_size_gt(SIZE_MAX - 1));
    printf("power_of_2_size_gt_simple(%zu): %zu\n", (size_t)SIZE_MAX - 1, power_of_2_size_gt_simple(SIZE_MAX - 1));
}


int
main(int argc, char **argv)
{
    (void)argc, (void)argv;

    test_mask_below_bit();
    //test_power_of_2_size_gt();
    //wtf();
    //wtf2();

    return EXIT_SUCCESS;
}
