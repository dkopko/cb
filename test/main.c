#include <assert.h>
#include <stdio.h>
#include "cb.h"
#include "cb_map.h"


static void
test_alignments(void)
{
#if 0
    for (unsigned int i = 0; i < 100; ++i)
        printf("Is power of 2? %d %d\n", i, is_power_of_2(i));

    printf("offset_aligned_gte(%d, %d): %ju\n", 7, 8, (uintmax_t)offset_aligned_gte(7, 8));
    printf("offset_aligned_gte(%d, %d): %ju\n", 8, 8, (uintmax_t)offset_aligned_gte(8, 8));
    printf("offset_aligned_gte(%d, %d): %ju\n", 9, 8, (uintmax_t)offset_aligned_gte(9, 8));
    printf("offset_aligned_gte(%d, %d): %ju\n", 0, 1024, (uintmax_t)offset_aligned_gte(0, 1024));
    printf("offset_aligned_gte(%d, %d): %ju\n", 1, 1024, (uintmax_t)offset_aligned_gte(1, 1024));
    printf("offset_aligned_gte(%d, %d): %ju\n", 7, 1024, (uintmax_t)offset_aligned_gte(7, 1024));
    printf("offset_aligned_gte(%d, %d): %ju\n", 1023, 1024, (uintmax_t)offset_aligned_gte(1023, 1024));
    printf("offset_aligned_gte(%d, %d): %ju\n", 1024, 1024, (uintmax_t)offset_aligned_gte(1024, 1024));
    printf("offset_aligned_gte(%d, %d): %ju\n", 1025, 1024, (uintmax_t)offset_aligned_gte(1025, 1024));
#endif
}


static void
test_raw_append(struct cb *cb)
{
    for (int i = 0; i < 50000; ++i)
    {
        cb_append(&cb, "THIS ", sizeof("THIS ") - 1);
        cb_append(&cb, "IS ", sizeof("IS ") - 1);
        cb_append(&cb, "A ", sizeof("A ") - 1);
        cb_append(&cb, "TEST ", sizeof("TEST ") - 1);
    }
}


static void
test_kv_set(struct cb **cb)
{
    struct cb_map cb_map;
    int ret;

    ret = cb_map_init(&cb_map, cb);
    assert(ret == 0);

    for (int i = 0; i < 50; ++i)
    {
        struct cb_key key;
        struct cb_value value;

        key.k = i;
        value.v = i * 2;

        ret = cb_map_kv_set(&cb_map, &key, &value);
        assert(ret == 0);
    }

    for (int i = 0; i < 200; i += 2)
    {
        struct cb_key key;
        struct cb_value value;

        key.k = i;
        value.v = i * 3;

        ret = cb_map_kv_set(&cb_map, &key, &value);
        assert(ret == 0);
    }

    struct cb_key key;
    struct cb_value value;
    key.k = 24;

    ret = cb_map_kv_lookup(&cb_map, &key, &value);
    if (ret != 0)
    {
        fprintf(stderr, "error, key not found\n");
        exit(EXIT_FAILURE);
    }

    printf("value of key %ju is %ju\n", (uintmax_t)key.k, (uintmax_t)value.v);

    ret = cb_map_kv_delete(&cb_map, &key);
    assert(ret == 0);

    ret = cb_map_kv_lookup(&cb_map, &key, &value);
    if (ret == -1)
        printf("as expected, key not found\n");
    assert(ret == -1);
}


static void
test_bst(struct cb **cb)
{
    struct cb_map cb_map;
    struct cb_key key;
    struct cb_value value;
    int ret;

    (void)ret;

    ret = cb_map_init(&cb_map, cb);
    assert(ret == 0);

    key.k = 1;
    value.v = 2;
    ret = cb_map_kv_set(&cb_map, &key, &value);
    assert(ret == 0);

    key.k = 2;
    value.v = 5;
    ret = cb_map_kv_set(&cb_map, &key, &value);
    assert(ret == 0);

    key.k = 3;
    value.v = 8;
    ret = cb_map_kv_set(&cb_map, &key, &value);
    assert(ret == 0);

    key.k = 2;
    ret = cb_map_kv_delete(&cb_map, &key);
    assert(ret == 0);

    printf("BEFORE cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");

    cb_map_consolidate(&cb_map);

    printf("AFTER cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");
}


static int
doprint(const struct cb_key *k, const struct cb_value *v, void *closure)
{
    int ret;

    (void)closure;

    ret = printf("doprint -- k: %ju, v: %ju\n",
                 (uintmax_t)k->k, (uintmax_t)v->v);
    return (ret < 0 ? ret : 0);
}


static void
test_bst2(struct cb **cb)
{
    //An exhaustive depth 4 tree.
    //int keys[] = { 15, 13, 11, 9, 7, 5, 3, 1, 14, 10, 6, 2, 12, 4, 8 };

    int keys[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    int keys2[] = { 16, 17, 18 };

    struct cb_map cb_map;
    struct cb_key key;
    struct cb_value value;
    int ret;

    (void)ret;

    ret = cb_map_init(&cb_map, cb);
    assert(ret == 0);

    for (unsigned int i = 0; i < (sizeof(keys)/sizeof(keys[0])); ++i)
    {
        key.k = keys[i];
        value.v = 99;

        ret = cb_map_kv_set(&cb_map, &key, &value);
        assert(ret == 0);
    }

    printf("BEFORE FIRST cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");

    cb_map_consolidate(&cb_map);

    printf("AFTER FIRST cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");

    for (unsigned int i = 0; i < (sizeof(keys2)/sizeof(keys2[0])); ++i)
    {
        key.k = keys2[i];
        value.v = 99;

        ret = cb_map_kv_set(&cb_map, &key, &value);
        assert(ret == 0);

        if (key.k == 17)
        {
            struct cb_key key_to_delete;
            key_to_delete.k = 5;

            ret = cb_map_kv_delete(&cb_map, &key_to_delete);
            assert(ret == 0);
        }
   }

    printf("BEFORE SECOND cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");

    cb_map_consolidate(&cb_map);

    printf("AFTER SECOND cb_map_consolidate():\n");
    cb_map_print(&cb_map);
    printf("\n");

    cb_map_traverse(&cb_map, doprint, NULL);
}


int main(int argc, char **argv)
{
    int ret;
    struct cb *cb;

    (void)argc;
    (void)argv;

    //test_alignments();

    ret = cb_module_init();
    if (ret != 0)
    {
        fprintf(stderr, "cb_module_init() failed.\n");
        return EXIT_FAILURE;
    }

    struct cb_params cb_params = CB_PARAMS_DEFAULT;
    cb_params.ring_size = 8192;
    cb_params.mmap_flags &= ~MAP_ANONYMOUS;
    cb = cb_create(&cb_params, sizeof(cb_params));
    if (!cb)
    {
        fprintf(stderr, "Could not create cb.\n");
        return EXIT_FAILURE;
    }

    //test_raw_append(cb);
    //test_kv_set(cb);
    //test_bst(cb);

    test_bst2(&cb);
    return EXIT_SUCCESS;
}

