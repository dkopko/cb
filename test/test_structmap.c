#include <stdio.h>
#include "cb.h"
#include "cb_structmap.h"

void
test_structmap(struct cb **cb)
{
    cb_offset_t structmap_root = CB_STRUCTMAP_SENTINEL;
    cb_offset_t v = 22;
    cb_offset_t v1 = 444,
                v2 = 444;
    int ret;

    cb_structmap_insert(cb, &structmap_root, 0, 0,            123);
    cb_structmap_insert(cb, &structmap_root, 0, UINT64_MAX/2, 456);
    cb_structmap_insert(cb, &structmap_root, 0, UINT64_MAX,   789);

    cb_log_debug("DANDEBUG begin structmap");
    cb_structmap_print(*cb, structmap_root);
    cb_log_debug("DANDEBUG end structmap");
    cb_log_debug("DANDEBUG2 structmap_root: %ju", (uintmax_t)structmap_root);
    cb_structmap_lookup(*cb, structmap_root, UINT64_MAX/2, &v);
    cb_log_debug("DANDEBUG2 v: %ju", (uintmax_t)v);


    ret = cb_structmap_lookup(*cb, structmap_root, UINT64_MAX/2, &v1);
    cb_log_debug("DANDEBUG2 ret: %d, v1: %ju", ret, (uintmax_t)v1);
    ret = cb_structmap_delete(cb, &structmap_root, 0, UINT64_MAX/2, NULL);
    cb_log_debug("DANDEBUG2 delete1 ret: %d", ret);

    ret = cb_structmap_lookup(*cb, structmap_root, UINT64_MAX/2, &v2);
    cb_log_debug("DANDEBUG2 ret: %d, v2: %ju", ret, (uintmax_t)v2);
    ret = cb_structmap_delete(cb, &structmap_root, 0, UINT64_MAX/2, NULL);
    cb_log_debug("DANDEBUG2 delete2 ret: %d", ret);
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

    test_structmap(&cb);
    return EXIT_SUCCESS;
}

