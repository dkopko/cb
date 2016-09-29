#include <assert.h>
#include <stdio.h>
#include "cb.h"
#include "cb_bst.h"
#include "cb_term.h"


int
main(int argc, char **argv)
{
    struct cb_params  cb_params = CB_PARAMS_DEFAULT;
    struct cb        *cb;
    cb_offset_t       bst_root = CB_BST_SENTINEL;
    struct cb_term    term_a,
                      term_b,
                      term_c;
    int ret;
    bool bret;

    (void)argc;
    (void)argv;
    (void)ret;
    (void)bret;


    /* Initialize library. */
    ret = cb_module_init();
    if (ret != 0)
    {
        fprintf(stderr, "cb_module_init() failed.\n");
        return EXIT_FAILURE;
    }


    /* Create CB. */
    cb_params.ring_size = 8192;
    cb_params.mmap_flags &= ~MAP_ANONYMOUS;
    cb = cb_create(&cb_params, sizeof(cb_params));
    if (!cb)
    {
        fprintf(stderr, "Could not create cb.\n");
        return EXIT_FAILURE;
    }


    /* Test insert. */
    cb_term_set_u64(&term_a, 1);
    cb_term_set_u64(&term_b, 10);
    ret = cb_bst_insert(&cb, &bst_root, 0, &term_a, &term_b);
    cb_assert(ret == 0);

    cb_term_set_u64(&term_a, 2);
    cb_term_set_u64(&term_b, 20);
    ret = cb_bst_insert(&cb, &bst_root, 0, &term_a, &term_b);
    cb_assert(ret == 0);

    cb_term_set_u64(&term_a, 3);
    cb_term_set_u64(&term_b, 30);
    ret = cb_bst_insert(&cb, &bst_root, 0, &term_a, &term_b);
    cb_assert(ret == 0);


    /* Test lookup success. */
    cb_term_set_u64(&term_a, 1);
    ret = cb_bst_lookup(cb, bst_root, &term_a, &term_c);
    cb_assert(ret == 0);
    cb_assert(cb_term_get_u64(&term_c) == 10);


    /* Test lookup failure. */
    cb_term_set_u64(&term_a, 99);
    ret = cb_bst_lookup(cb, bst_root, &term_a, &term_c);
    cb_assert(ret != 0);


    /* Test insert overwrites. */
    cb_term_set_u64(&term_a, 4);
    cb_term_set_u64(&term_b, 39);
    ret = cb_bst_insert(&cb, &bst_root, 0, &term_a, &term_b);
    cb_assert(ret == 0);

    cb_term_set_u64(&term_a, 4);
    ret = cb_bst_lookup(cb, bst_root, &term_a, &term_c);
    cb_assert(ret == 0);
    cb_assert(cb_term_get_u64(&term_c) == 39);

    cb_term_set_u64(&term_a, 4);
    cb_term_set_u64(&term_b, 40);
    ret = cb_bst_insert(&cb, &bst_root, 0, &term_a, &term_b);
    cb_assert(ret == 0);

    cb_term_set_u64(&term_a, 4);
    ret = cb_bst_lookup(cb, bst_root, &term_a, &term_c);
    cb_assert(ret == 0);
    cb_assert(cb_term_get_u64(&term_c) == 40);


    /* Test delete success. */
    cb_term_set_u64(&term_a, 2);
    ret = cb_bst_delete(&cb, &bst_root, 0, &term_a);
    cb_assert(ret == 0);


    /* Test delete failure. */
    cb_term_set_u64(&term_a, 99);
    ret = cb_bst_delete(&cb, &bst_root, 0, &term_a);
    cb_assert(ret != 0);


    /* Test contains key. */
    cb_term_set_u64(&term_a, 3);
    bret = cb_bst_contains_key(cb, bst_root, &term_a);
    cb_assert(bret);


    /* Test does not contain key. */
    cb_term_set_u64(&term_a, 99);
    bret = cb_bst_contains_key(cb, bst_root, &term_a);
    cb_assert(!bret);


    /* Test traversal FIXME. */


    /* Test print. */
    cb_bst_print(&cb, bst_root);


    /* Test comparison, less than. */
    /* Test comparison, equal to. */
    /* Test comparison, greater than. */


    /* Test size. */
    {
        cb_offset_t    bst1 = CB_BST_SENTINEL,
                       bst2 = CB_BST_SENTINEL;
        struct cb_term key1,
                       key2,
                       key3,
                       key4,
                       value1,
                       value2,
                       value3,
                       value4;
        size_t         empty_size,
                       header_size,
                       node_size,
                       size1,
                       size2,
                       size3,
                       size4,
                       size5,
                       bst1_size;

        (void)size3, (void)size4, (void)size5, (void)bst1_size;

        cb_term_set_u64(&key1, 111);
        cb_term_set_u64(&key2, 222);
        cb_term_set_u64(&key3, 333);
        cb_term_set_u64(&key4, 444);
        cb_term_set_u64(&value1, 1);
        cb_term_set_u64(&value2, 2);
        cb_term_set_u64(&value3, 3);

        empty_size = cb_bst_size(cb, bst1);
        cb_assert(empty_size == 0);

        ret = cb_bst_insert(&cb, &bst1, 0, &key1, &value1);
        cb_assert(ret == 0);
        size1 = cb_bst_size(cb, bst1);

        ret = cb_bst_insert(&cb, &bst1, 0, &key2, &value2);
        cb_assert(ret == 0);
        size2 = cb_bst_size(cb, bst1);

        node_size = (size2 - size1);
        header_size = (size1 - empty_size) - node_size;
        cb_assert(size1 == header_size + node_size);
        printf("header_size: %zu\n", header_size);
        printf("node_size: %zu\n", node_size);

        ret = cb_bst_insert(&cb, &bst1, 0, &key3, &value3);
        cb_assert(ret == 0);
        size3 = cb_bst_size(cb, bst1);
        cb_assert(size3 - size2 == node_size);

        ret = cb_bst_delete(&cb, &bst1, 0, &key2);
        cb_assert(ret == 0);
        size4 = cb_bst_size(cb, bst1);
        cb_assert(size4 == size2);

        bst1_size = cb_bst_size(cb, bst1);
        cb_term_set_bst(&value4, bst1);

        ret = cb_bst_insert(&cb, &bst2, 0, &key4, &value4);
        cb_assert(ret == 0);
        size5 = cb_bst_size(cb, bst2);
        cb_assert(size5 == header_size + node_size + bst1_size);
    }


    /* Test render. */
    {
        cb_offset_t dest_offset;
        const char *str;

        ret = cb_bst_render(&dest_offset, &cb, bst_root, 0);
        cb_assert(ret == 0);
        str = (const char *)cb_at(cb, dest_offset);
        printf("BST rendered: \"%s\"\n", str);
    }

    /* Test to string. */
    {
        const char *str;
        str = cb_bst_to_str(&cb, bst_root);
        cb_assert(str != NULL);
        cb_assert(strlen(str) > 0);
        printf("BST as string: \"%s\"\n", str);
    }

    return EXIT_SUCCESS;
}

