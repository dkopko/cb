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
    printf("Size: %zu\n", cb_bst_size(cb, bst_root));


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

