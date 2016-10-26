#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include <vector>
namespace {
extern "C" {
#include "cb.h"
#include "cb_bst.h"
#include "cb_map.h"
#include "cb_random.h"
};
};
#include "external/cycle.h"


enum event_op
{
    EOP_INSERT_UNKNOWN,
    EOP_INSERT_KNOWN,
    EOP_REMOVE_UNKNOWN,
    EOP_REMOVE_KNOWN,
    EOP_LOOKUP_UNKNOWN,
    EOP_LOOKUP_KNOWN,
    EOP_MAX
};


struct event
{
    enum event_op op;
    bool omit;
    uint64_t t0;
    uint64_t t1;
    union
    {
        struct
        {
            uint64_t k;
            uint64_t v;
        } insert;
        struct
        {
            uint64_t k;
        } remove;
        struct
        {
            uint64_t k;
        } lookup;
    };
};


struct map_impl
{
    char const* (*name_cb)(void);
    void* (*create_cb)(int argc, char **argv);
    void (*destroy_cb)(void *closure);
    void (*handle_events_cb)(struct event *events,
                             unsigned int  events_count,
                             void         *closure);
};


typedef std::map<uint64_t, uint64_t> stdmap_impl_t;


static char const* stdmap_name(void)
{
    return "stdmap";
}


static void* stdmap_create(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return new stdmap_impl_t;
}


static void stdmap_destroy(void *closure)
{
    stdmap_impl_t *map = static_cast<stdmap_impl_t*>(closure);

    delete map;
}


static void stdmap_handle_events(struct event *events,
                                 unsigned int  events_count,
                                 void         *closure)
{
    stdmap_impl_t *map = static_cast<stdmap_impl_t*>(closure);

    for (unsigned int i = 0; i < events_count; ++i)
    {
        struct event *e = &events[i];

        if (e->omit)
            continue;

        switch (e->op)
        {
            case EOP_INSERT_UNKNOWN:
            case EOP_INSERT_KNOWN:
                e->t0 = getticks();
                (*map)[e->insert.k] = e->insert.v;
                e->t1 = getticks();
                break;

            case EOP_REMOVE_UNKNOWN:
            case EOP_REMOVE_KNOWN:
                e->t0 = getticks();
                map->erase(e->remove.k);
                e->t1 = getticks();
                break;

            case EOP_LOOKUP_UNKNOWN:
            case EOP_LOOKUP_KNOWN:
                e->t0 = getticks();
                map->find(e->lookup.k);
                e->t1 = getticks();
                break;

            default:
                assert(e->op < EOP_MAX);
        }
    }
}


static struct map_impl stdmap_impl = {
    .name_cb          = stdmap_name,
    .create_cb        = stdmap_create,
    .destroy_cb       = stdmap_destroy,
    .handle_events_cb = stdmap_handle_events
};


struct cbbst_state
{
    struct cb        *cb;
    struct cb_params  params;
    cb_offset_t root;
};


static char const* cbbst_name(void)
{
    return "cbbst";
}


static void* cbbst_create(int argc, char **argv)
{
    static struct option opts[] = {
        { "ring-size",   required_argument, NULL, 'r' },
        { 0, 0, 0, 0 }
    };
    struct cbbst_state *state;
    int c, longindex;
    bool bad_option = false;
    int ret;

    state = (struct cbbst_state*)calloc(1, sizeof(struct cbbst_state));
    if (state == NULL)
        return NULL;

    state->params = CB_PARAMS_DEFAULT;
    state->params.flags |= CB_PARAMS_F_LEAVE_FILES;
    state->params.ring_size = 8192;
    state->params.mmap_flags &= ~MAP_ANONYMOUS;
    state->params.mmap_flags &= ~MAP_SHARED;
    state->params.mmap_flags |= MAP_PRIVATE;
    state->params.mmap_flags |= MAP_POPULATE;

    optind = 1;
    opterr = 0;
    while ((c = getopt_long(argc, argv, "", opts, &longindex)) != -1)
    {
        switch (c)
        {
            case 'r':
            {
                ret = sscanf(optarg, "%zu", &(state->params.ring_size));
                if (ret == 1)
                    continue;
            }
            break;

            case '?':
            default:
                /* Ignore unknown options. */
                cb_log_debug("Unknown option \"%s\"", argv[optind-1]);
                continue;
        }

        cb_log_error("Could not parse --%s argument \"%s\"",
                     opts[longindex].name, optarg);
        bad_option = true;
    }

    if (bad_option)
    {
        cb_log_error("Terminating due to bad options.");
        exit(EXIT_FAILURE);
    }

    ret = cb_module_init();
    assert(ret == 0);

    state->cb = cb_create(&state->params, sizeof(state->params));
    if (!state->cb)
    {
        cb_log_error("Could not create cb.\n");
        return NULL;
    }

    state->root = CB_BST_SENTINEL;

    return state;
}


static void cbbst_destroy(void *closure)
{
    struct cbbst_state *state = static_cast<struct cbbst_state*>(closure);

    cb_destroy(state->cb);
}


static void cbbst_handle_events(struct event *events,
                                unsigned int  events_count,
                                void         *closure)
{
    struct cbbst_state *state = static_cast<struct cbbst_state*>(closure);
    struct cb_term key;
    struct cb_term value;
    int ret;

    /*FIXME there is a bug when using 0 instead of cb_cursor() for cutoff_offset
     * for in-place modifications.
     */

    for (unsigned int i = 0; i < events_count; ++i)
    {
        struct event *e = &events[i];

        if (e->omit)
            continue;

        switch (e->op)
        {
            case EOP_INSERT_UNKNOWN:
            case EOP_INSERT_KNOWN:
                cb_log_debug("=========== EVENT[%u]: EOP_INSERT (k: %ju, v:%ju)", i, (uintmax_t)e->insert.k, (uintmax_t)e->insert.v);
                cb_term_set_u64(&key, e->insert.k);
                cb_term_set_u64(&value, e->insert.v);
                e->t0 = getticks();
                ret = cb_bst_insert(&(state->cb),
                                    &(state->root),
                                    0,
                                    &key,
                                    &value);
                e->t1 = getticks();
                if (ret != 0)
                    abort();
                break;

            case EOP_REMOVE_UNKNOWN:
            case EOP_REMOVE_KNOWN:
                cb_log_debug("=========== EVENT[%u]: EOP_REMOVE (k: %ju)", i, (uintmax_t)e->remove.k);
                cb_term_set_u64(&key, e->remove.k);
                e->t0 = getticks();
                ret = cb_bst_delete(&(state->cb),
                                    &(state->root),
                                    0,
                                    &key);
                e->t1 = getticks();
                if (ret != 0 && e->op != EOP_REMOVE_UNKNOWN)
                    abort();
                break;

            case EOP_LOOKUP_UNKNOWN:
            case EOP_LOOKUP_KNOWN:
                cb_log_debug("=========== EVENT[%u]: EOP_LOOKUP (k: %ju)", i, (uintmax_t)e->lookup.k);
                cb_term_set_u64(&key, e->lookup.k);
                e->t0 = getticks();
                ret = cb_bst_lookup(state->cb,
                                    state->root,
                                    &key,
                                    &value);
                e->t1 = getticks();
                if (ret != 0 && e->op != EOP_LOOKUP_UNKNOWN)
                    abort();
                break;

            default:
                assert(e->op < EOP_MAX);
        }
    }
}


static struct map_impl cbbst_impl = {
    .name_cb          = cbbst_name,
    .create_cb        = cbbst_create,
    .destroy_cb       = cbbst_destroy,
    .handle_events_cb = cbbst_handle_events
};


struct cbmap_impl_state
{
    struct cb        *cb;
    struct cb_map     map;
    struct cb_params  params;
    unsigned int      total_count;
    unsigned int      mutate_count;
    unsigned int      consolidate_count;
};


static char const* cbmap_name(void)
{
    return "cbmap";
}


static void* cbmap_create(int argc, char **argv)
{
    static struct option opts[] = {
        { "consolidate", required_argument, NULL, 'c' },
        { "ring-size",   required_argument, NULL, 'r' },
        { 0, 0, 0, 0 }
    };
    struct cbmap_impl_state *state;
    int c, longindex;
    bool bad_option = false;
    int ret;

    state =
        (struct cbmap_impl_state*)calloc(1, sizeof(struct cbmap_impl_state));
    if (state == NULL)
        return NULL;

    state->params = CB_PARAMS_DEFAULT;
    state->params.flags |= CB_PARAMS_F_LEAVE_FILES;
    state->params.ring_size = 8192;
    state->params.mmap_flags &= ~MAP_ANONYMOUS;

    state->total_count = 0;
    state->mutate_count = 0;
    state->consolidate_count = 1;

    optind = 1;
    opterr = 0;
    while ((c = getopt_long(argc, argv, "", opts, &longindex)) != -1)
    {
        switch (c)
        {
            case 'c':
            {
                ret = sscanf(optarg, "%u", &(state->consolidate_count));
                if (ret == 1)
                    continue;
            }
            break;

            case 'r':
            {
                ret = sscanf(optarg, "%zu", &(state->params.ring_size));
                if (ret == 1)
                    continue;
            }
            break;

            case '?':
            default:
                /* Ignore unknown options. */
                cb_log_debug("Unknown option \"%s\"", argv[optind-1]);
                continue;
        }

        cb_log_error("Could not parse --%s argument \"%s\"",
                     opts[longindex].name, optarg);
        bad_option = true;
    }

    if (bad_option)
    {
        cb_log_error("Terminating due to bad options.");
        exit(EXIT_FAILURE);
    }

    ret = cb_module_init();
    assert(ret == 0);

    state->cb = cb_create(&state->params, sizeof(state->params));
    if (!state->cb)
    {
        cb_log_error("Could not create cb.\n");
        return NULL;
    }

    ret = cb_map_init(&(state->map), &(state->cb));
    if (ret != 0)
        return NULL;

    return state;
}


static void cbmap_destroy(void *closure)
{
    struct cbmap_impl_state *state =
        static_cast<struct cbmap_impl_state*>(closure);

    cb_destroy(state->cb);
}


static void cbmap_handle_events(struct event *events,
                                unsigned int  events_count,
                                void         *closure)
{
    struct cbmap_impl_state *state =
        static_cast<struct cbmap_impl_state*>(closure);
    int ret;

    for (unsigned int i = 0; i < events_count; ++i)
    {
        struct event *e = &events[i];

        if (e->omit)
            continue;

        switch (e->op)
        {
            case EOP_INSERT_UNKNOWN:
            case EOP_INSERT_KNOWN:
                {
                    cb_log_debug("=========== EVENT[%u]: EOP_INSERT (k: %ju, v:%ju)", i, (uintmax_t)e->insert.k, (uintmax_t)e->insert.v);
                    struct cb_term key;
                    struct cb_term value;
                    cb_term_set_u64(&key, e->insert.k);
                    cb_term_set_u64(&value, e->insert.v);
                    e->t0 = getticks();
                    ret = cb_map_kv_set(&(state->map), &key, &value);
                    e->t1 = getticks();
                    if (ret != 0)
                        abort();
                    ++(state->total_count);
                    ++(state->mutate_count);
                    if (state->mutate_count == state->consolidate_count)
                    {
                        cb_log_debug("DANDEBUG mutate_count == consolidate_count (%ju), consolidating.", (uintmax_t)state->consolidate_count);
                        state->mutate_count = 0;
                        cb_map_consolidate(&(state->map));
                        cb_log_debug("DANDEBUG done consolidating.");
                    }
                }
                break;

            case EOP_REMOVE_UNKNOWN:
            case EOP_REMOVE_KNOWN:
                {
                    cb_log_debug("=========== EVENT[%u]: EOP_REMOVE (k: %ju)", i, (uintmax_t)e->remove.k);
                    struct cb_term key;
                    cb_term_set_u64(&key, e->remove.k);
                    e->t0 = getticks();
                    ret = cb_map_kv_delete(&(state->map), &key);
                    e->t1 = getticks();
                    if (ret != 0 && e->op != EOP_REMOVE_UNKNOWN)
                        abort();
                    ++(state->total_count);
                    ++(state->mutate_count);
                    if (state->mutate_count == state->consolidate_count)
                    {
                        cb_log_debug("DANDEBUG mutate_count == consolidate_count (%ju), consolidating.", (uintmax_t)state->consolidate_count);
                        state->mutate_count = 0;
                        cb_map_consolidate(&(state->map));
                        cb_log_debug("DANDEBUG done consolidating.");
                    }
                }
                break;

            case EOP_LOOKUP_UNKNOWN:
            case EOP_LOOKUP_KNOWN:
                {
                    cb_log_debug("=========== EVENT[%u]: EOP_LOOKUP (k: %ju)", i, (uintmax_t)e->lookup.k);
                    struct cb_term key;
                    struct cb_term value;
                    cb_term_set_u64(&key, e->lookup.k);
                    e->t0 = getticks();
                    ret = cb_map_kv_lookup(&(state->map), &key, &value);
                    e->t1 = getticks();
                    if (ret != 0 && e->op != EOP_LOOKUP_UNKNOWN)
                        abort();
                }
                break;

            default:
                assert(e->op < EOP_MAX);
        }
    }
}


static struct map_impl cbmap_impl = {
    .name_cb          = cbmap_name,
    .create_cb        = cbmap_create,
    .destroy_cb       = cbmap_destroy,
    .handle_events_cb = cbmap_handle_events
};


struct known_set
{
    std::map<uint64_t, std::vector<uint64_t>::size_type> keys_map;
    std::vector<uint64_t> keys_vec;
};
typedef struct known_set known_set_t;


static void
known_set_init(known_set_t *ks, size_t prealloc_count)
{
    ks->keys_vec.reserve(prealloc_count);
}


static size_t
known_set_count(known_set_t *ks)
{
    if (ks->keys_map.size() != ks->keys_vec.size())
        cb_log_debug("DANDEBUG WTF %zu %zu", ks->keys_map.size(), ks->keys_vec.size());

    assert(ks->keys_map.size() == ks->keys_vec.size());
    return ks->keys_vec.size();
}


static bool
known_set_contains(known_set_t *ks, uint64_t k)
{
    return ks->keys_map.find(k) != ks->keys_map.end();
}


static void
known_set_insert(known_set_t *ks, uint64_t k)
{
    size_t set_count = known_set_count(ks);;

    if (known_set_contains(ks, k))
        return;

    ks->keys_map[k] = set_count;
    ks->keys_vec.push_back(k);
}


static void
known_set_remove(known_set_t *ks, uint64_t k)
{
    std::vector<uint64_t>::size_type k_pos;
    uint64_t last_k;

    if (!known_set_contains(ks, k))
        return;

    k_pos  = ks->keys_map[k];
    last_k = ks->keys_vec[ks->keys_vec.size() - 1];

    ks->keys_map[last_k] = k_pos;
    ks->keys_vec[k_pos] = last_k;

    ks->keys_map.erase(k);
    ks->keys_vec.pop_back();
}


static uint64_t
known_set_get_random(known_set_t *ks, struct cb_random_state *rs)
{
    assert(known_set_count(ks) > 0);
    return ks->keys_vec[cb_random_next_range(rs, known_set_count(ks))];
}


static void
print_event(struct event *e)
{
    printf("%ju ", (uintmax_t)(e->t1 - e->t0));

    switch (e->op)
    {
        case EOP_INSERT_UNKNOWN:
            printf("INSERT_UNKNOWN %" PRIu64 " %" PRIu64 "\n",
                   e->insert.k, e->insert.v);
            break;

        case EOP_INSERT_KNOWN:
            printf("INSERT_KNOWN %" PRIu64 " %" PRIu64 "\n",
                   e->insert.k, e->insert.v);
            break;

        case EOP_REMOVE_UNKNOWN:
            printf("REMOVE_UNKNOWN %" PRIu64 "\n", e->remove.k);
            break;

        case EOP_REMOVE_KNOWN:
            printf("REMOVE_KNOWN %" PRIu64 "\n", e->remove.k);
            break;

        case EOP_LOOKUP_UNKNOWN:
            printf("LOOKUP_UNKNOWN %" PRIu64 "\n", e->lookup.k);
            break;

        case EOP_LOOKUP_KNOWN:
            printf("LOOKUP_KNOWN %" PRIu64 "\n", e->lookup.k);
            break;

        default:
            abort();
    }
}


static void
generate_insert_events(struct event           *events,
                       uint32_t                num_events,
                       known_set_t            *known_set,
                       struct cb_random_state *rs)
{
    for (uint32_t i = 0; i < num_events; ++i)
    {
        events[i].op = EOP_INSERT_UNKNOWN;
        do
        {
            events[i].insert.k = cb_random_next(rs) % (num_events * 10);
        } while(known_set_contains(known_set, events[i].insert.k));
        known_set_insert(known_set, events[i].insert.k);
        events[i].insert.v = 888;
    }
}


static void
generate_random_events(struct event           *events,
                       uint32_t                num_events,
                       known_set_t            *known_set,
                       struct cb_random_state *rs,
                       enum event_op          *ratios,
                       unsigned int            ratios_len,
                       bool                    allow_omit)
{
    for (uint32_t i = 0; i < num_events; ++i)
    {
        events[i].op = ratios[cb_random_next_range(rs, ratios_len)];
        events[i].omit = false;

        switch (events[i].op)
        {
            case EOP_INSERT_KNOWN:
                if (known_set_count(known_set) == 0)
                {
                    if (!allow_omit)
                    {
                        cb_log_error("knownset empty.");
                        exit(EXIT_FAILURE);
                    }

                    events[i].omit = true;
                    break;
                }
                events[i].insert.k = known_set_get_random(known_set, rs);
                events[i].insert.v = 777;
                break;

            case EOP_INSERT_UNKNOWN:
                do
                {
                    events[i].insert.k = cb_random_next(rs) % (num_events * 10);
                } while(known_set_contains(known_set, events[i].insert.k));
                known_set_insert(known_set, events[i].insert.k);
                events[i].insert.v = 888;
                break;

            case EOP_REMOVE_KNOWN:
                if (known_set_count(known_set) == 0)
                {
                    if (!allow_omit)
                    {
                        cb_log_error("knownset empty.");
                        exit(EXIT_FAILURE);
                    }

                    events[i].omit = true;
                    break;
                }
                events[i].remove.k = known_set_get_random(known_set, rs);
                known_set_remove(known_set, events[i].remove.k);
                break;

            case EOP_REMOVE_UNKNOWN:
                do
                {
                    events[i].remove.k = cb_random_next(rs);
                } while(known_set_contains(known_set, events[i].remove.k));
                break;

            case EOP_LOOKUP_KNOWN:
                if (known_set_count(known_set) == 0)
                {
                    if (!allow_omit)
                    {
                        cb_log_error("knownset empty.");
                        exit(EXIT_FAILURE);
                    }

                    events[i].omit = true;
                    break;
                }
                events[i].lookup.k = known_set_get_random(known_set, rs);
                break;

            case EOP_LOOKUP_UNKNOWN:
                do
                {
                    events[i].lookup.k = cb_random_next(rs) % (num_events * 10);
                } while(known_set_contains(known_set, events[i].remove.k));
                break;

            default:
                assert(events[i].op < EOP_MAX);
        }
    }

    cb_log_debug("Done generating events.");
}


static void
print_vector_hist(char const                  *map_name,
                  char const                  *event_name,
                  std::vector<uint64_t> const *vec)
{
    std::map<uint64_t, uint64_t> hist;
    //static const uint64_t bucket_width = 100; /*ticks*/
    static const uint64_t bucket_width = 1; /*ticks*/

    for (std::vector<uint64_t>::size_type i = 0; i < vec->size(); ++i)
    {
        hist[(*vec)[i] / bucket_width]++;
    }


    for (std::map<uint64_t, uint64_t>::const_iterator h_i = hist.begin(),
                                                      h_e = hist.end();
         h_i != h_e;
         ++h_i)
    {
        printf("HIST %s %s %ju %ju\n",
               map_name,
               event_name,
               (uintmax_t)h_i->first,
               (uintmax_t)h_i->second);
    }
}


static void
print_vector_stats(char const                  *map_name,
                   char const                  *event_name,
                   std::vector<uint64_t> const *vec)
{
    std::vector<uint64_t> sorted_vec(*vec);
    double sum = 0.0, sum_squared = 0.0;

    std::sort(sorted_vec.begin(), sorted_vec.end());

    for (std::vector<uint64_t>::size_type i = 0; i < sorted_vec.size(); ++i)
    {
        sum += sorted_vec[i];
        sum_squared += (double)sorted_vec[i] * (double)sorted_vec[i];
    }

    double percentile99    = (sorted_vec.size() == 0 ? 1.0/0.0 : sorted_vec[(int)(0.99 * sorted_vec.size() - 0.5)]);
    double percentile999   = (sorted_vec.size() == 0 ? 1.0/0.0 : sorted_vec[(int)(0.999 * sorted_vec.size() - 0.5)]);
    double percentile9999  = (sorted_vec.size() == 0 ? 1.0/0.0 : sorted_vec[(int)(0.9999 * sorted_vec.size() - 0.5)]);
    double percentile99999 = (sorted_vec.size() == 0 ? 1.0/0.0 : sorted_vec[(int)(0.99999 * sorted_vec.size() - 0.5)]);

    if (sorted_vec.size() == 0)
        return;

    printf("TEST_RESULT %s %s"
           " Count: %ju,"
           " Mean: %.0f,"
           " Std: %.0f,"
           " Min: %.0f,"
           " Max: %.0f,"
           " 99%%ile: %.0f,"
           " 99.9%%ile: %.0f,"
           " 99.99%%ile: %.0f,"
           " 99.999%%ile: %.0f"
           "\n",
           map_name, event_name,
           (uintmax_t)sorted_vec.size(),
           sum / sorted_vec.size(),
           sqrt(sum_squared / sorted_vec.size() - (sum / sorted_vec.size()) * (sum / sorted_vec.size())),
           sorted_vec.size() == 0 ? NAN : (double)sorted_vec[0],
           sorted_vec.size() == 0 ? NAN : (double)sorted_vec[sorted_vec.size() - 1],
           percentile99,
           percentile999,
           percentile9999,
           percentile99999);
}


static void
print_stats(char const        *map_name,
            struct event      *events,
            uint32_t           num_events)
{
    std::vector<uint64_t> insert_unknown_deltas,
                          insert_known_deltas,
                          remove_unknown_deltas,
                          remove_known_deltas,
                          lookup_unknown_deltas,
                          lookup_known_deltas;

    for (uint32_t i = 0; i < num_events; ++i)
    {
        if (events[i].omit)
            continue;

        assert(events[i].t0 < events[i].t1);

        switch (events[i].op)
        {
            case EOP_INSERT_UNKNOWN:
                insert_unknown_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            case EOP_INSERT_KNOWN:
                insert_known_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            case EOP_REMOVE_UNKNOWN:
                remove_unknown_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            case EOP_REMOVE_KNOWN:
                remove_known_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            case EOP_LOOKUP_UNKNOWN:
                lookup_unknown_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            case EOP_LOOKUP_KNOWN:
                lookup_known_deltas.push_back(events[i].t1 - events[i].t0);
                break;
            default:
                cb_log_error("Unknown event op: %ju", events[i].op);
                abort();
        }
    }

    print_vector_hist(map_name, "EOP_INSERT_UNKNOWN", &insert_unknown_deltas);
    print_vector_hist(map_name, "EOP_INSERT_KNOWN",   &insert_known_deltas);
    print_vector_hist(map_name, "EOP_REMOVE_UNKNOWN", &remove_unknown_deltas);
    print_vector_hist(map_name, "EOP_REMOVE_KNOWN",   &remove_known_deltas);
    print_vector_hist(map_name, "EOP_LOOKUP_UNKNOWN", &lookup_unknown_deltas);
    print_vector_hist(map_name, "EOP_LOOKUP_KNOWN",   &lookup_known_deltas);

    print_vector_stats(map_name, "EOP_INSERT_UNKNOWN", &insert_unknown_deltas);
    print_vector_stats(map_name, "EOP_INSERT_KNOWN",   &insert_known_deltas);
    print_vector_stats(map_name, "EOP_REMOVE_UNKNOWN", &remove_unknown_deltas);
    print_vector_stats(map_name, "EOP_REMOVE_KNOWN",   &remove_known_deltas);
    print_vector_stats(map_name, "EOP_LOOKUP_UNKNOWN", &lookup_unknown_deltas);
    print_vector_stats(map_name, "EOP_LOOKUP_KNOWN",   &lookup_known_deltas);
}


static void print_help(char const *progname)
{
    printf("Usage:\t%s [OPTION]...\n"
           "General Options:\n"
           "\t--pre-insert <n>\n"
           "\t--event-count\n"
           "\t--impl <implementations>\n"
           "\t\t(where implementations: stdmap,cbbst,cbmap\n"
           "\t--ratios <ratios>\n"
           "\t\t(where ratios: <insert_unknown>,<insert_known>,<remove_unknown>,<remove_known>,<lookup_unknown>,<lookup_known>)\n"
           "\t--seed <n>\n"
           "\t--help\n"
           "cb_map-Specific Options:\n"
           "\t--consolidate <n>\n"
           "\t--ring-size <n>\n",
           progname);
}

int main(int argc, char **argv)
{
    static struct option opts[] = {
        { "ratios",      required_argument, NULL, 'a' }, /* main */
        { "consolidate", required_argument, NULL, 'c' }, /* cb_map */
        { "event-count", required_argument, NULL, 'e' }, /* main */
        { "help",        no_argument,       NULL, 'h' }, /* main */
        { "impl",        required_argument, NULL, 'i' }, /* main */
        { "pre-insert",  required_argument, NULL, 'p' }, /* main */
        { "ring-size",   required_argument, NULL, 'r' }, /* cb_map */
        { "seed",        required_argument, NULL, 's' }, /* main */
        { 0, 0, 0, 0 }
    };
    static struct map_impl ALL_IMPLS[] = { stdmap_impl, cbbst_impl, cbmap_impl };
    struct cb_random_state rs;
    struct event *events,
                 *postremove_events;
    uint32_t num_preinsert_events = 0,
             num_events           = 100000,
             num_postremove_events,
             total_num_events;
    uint64_t seed = 0;
    uint16_t ratio_insert_unknown = 2,
             ratio_insert_known   = 0,
             ratio_remove_unknown = 0,
             ratio_remove_known   = 1,
             ratio_lookup_unknown = 0,
             ratio_lookup_known   = 0;
    known_set_t known_set;
    std::vector<struct map_impl> impls;
    std::vector<enum event_op> ratios;
    enum event_op remove_op = EOP_REMOVE_KNOWN;
    int c, longindex;
    bool bad_option = false;
    int ret;

    /* Parse arguments */
    optind = 1;
    opterr = 0;
    while ((c = getopt_long(argc, argv, "", opts, &longindex)) != -1)
    {
        switch (c)
        {
            case 'a':
            {
                uint16_t new_ratio_insert_unknown = 0,
                         new_ratio_insert_known   = 0,
                         new_ratio_remove_unknown = 0,
                         new_ratio_remove_known   = 0,
                         new_ratio_lookup_unknown = 0,
                         new_ratio_lookup_known   = 0;

                ret = sscanf(optarg,
                             "%" SCNu16 ",%" SCNu16 ",%" SCNu16
                             ",%" SCNu16 ",%" SCNu16 ",%" SCNu16,
                             &new_ratio_insert_unknown,
                             &new_ratio_insert_known,
                             &new_ratio_remove_unknown,
                             &new_ratio_remove_known,
                             &new_ratio_lookup_unknown,
                             &new_ratio_lookup_known);
                if (ret > 0)
                {
                    ratio_insert_unknown = new_ratio_insert_unknown;
                    ratio_insert_known   = new_ratio_insert_known;
                    ratio_remove_unknown = new_ratio_remove_unknown;
                    ratio_remove_known   = new_ratio_remove_known;
                    ratio_lookup_unknown = new_ratio_lookup_unknown;
                    ratio_lookup_known   = new_ratio_lookup_known;
                    continue;
                }
            }
            break;

            case 'c':
                /* Ignored, cb_map usage. */
                continue;

            case 'e':
            {
                ret = sscanf(optarg, "%" SCNu32, &num_events);
                if (ret == 1)
                    continue;
            }
            break;

            case 'h':
                print_help(argv[0]);
                exit(EXIT_SUCCESS);
                break;

            case 'i':
            {
                char *implname;

                while ((implname = strsep(&optarg, ",")) != NULL)
                {
                    bool found = false;

                    for (unsigned int i = 0;
                         i < sizeof(ALL_IMPLS)/sizeof(ALL_IMPLS[0]);
                         ++i)
                    {
                        if (strcmp(implname, ALL_IMPLS[i].name_cb()) == 0)
                        {
                            impls.push_back(ALL_IMPLS[i]);
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        fprintf(stderr, "Unrecognized impl: \"%s\"\n",
                                implname);
                        exit(EXIT_FAILURE);
                    }
                }

                continue;
            }

            case 'p':
            {
                ret = sscanf(optarg, "%" SCNu32, &num_preinsert_events);
                if (ret == 1)
                    continue;
            }
            break;

            case 'r':
                /* Ignored, cb_map usage. */
                continue;

            case 's':
            {
                ret = sscanf(optarg, "%" SCNu64, &seed);
                if (ret == 1)
                    continue;
            }
            break;

            case '?':
            default:
                /* Ignore unknown options. */
                cb_log_error("Unknown option \"%s\"", argv[optind-1]);
                continue;
        }

        cb_log_error("Could not parse --%s argument \"%s\"",
                     opts[longindex].name, optarg);
        bad_option = true;
    }

    if (bad_option)
    {
        cb_log_error("Terminating due to bad options.");
        exit(EXIT_FAILURE);
    }

    total_num_events = num_preinsert_events + num_events;

    /* Default implementations if none chosen. */
    if (impls.empty())
        impls.insert(impls.end(),
                     &ALL_IMPLS[0],
                     &ALL_IMPLS[sizeof(ALL_IMPLS)/sizeof(ALL_IMPLS[0])]);

    /* Setup ratios of events. */
    for (uint16_t i = 0; i < ratio_insert_unknown; ++i)
        ratios.push_back(EOP_INSERT_UNKNOWN);

    for (uint16_t i = 0; i < ratio_insert_known; ++i)
        ratios.push_back(EOP_INSERT_KNOWN);

    for (uint16_t i = 0; i < ratio_remove_unknown; ++i)
        ratios.push_back(EOP_REMOVE_UNKNOWN);

    for (uint16_t i = 0; i < ratio_remove_known; ++i)
        ratios.push_back(EOP_REMOVE_KNOWN);

    for (uint16_t i = 0; i < ratio_lookup_unknown; ++i)
        ratios.push_back(EOP_LOOKUP_UNKNOWN);

    for (uint16_t i = 0; i < ratio_lookup_known; ++i)
        ratios.push_back(EOP_LOOKUP_KNOWN);


    if (ratios.size() == 0)
    {
        cb_log_error("Cannot run with all-zero ratios.");
        exit(EXIT_FAILURE);
    }

    /* Generate the events list. */
    known_set_init(&known_set, num_events);

    events = (struct event *)malloc(total_num_events * sizeof(struct event));
    if (!events)
    {
        cb_log_error("Could not allocate events array.");
        exit(EXIT_FAILURE);
    }

    cb_random_state_init(&rs, seed);
    generate_insert_events(events,
                           num_preinsert_events,
                           &known_set,
                           &rs);

    cb_random_state_init(&rs, seed);
    generate_random_events(&events[num_preinsert_events],
                           num_events,
                           &known_set,
                           &rs,
                           ratios.data(),
                           ratios.size(),
                           true);

    /* Build cleanup stage. */
    num_postremove_events = known_set_count(&known_set);
    postremove_events = (struct event *)malloc(num_postremove_events * sizeof(struct event));
    if (!postremove_events)
    {
        cb_log_error("Could not allocate post-remove events array.");
        exit(EXIT_FAILURE);
    }
    generate_random_events(postremove_events,
                           num_postremove_events,
                           &known_set,
                           &rs,
                           &remove_op,
                           1,
                           false);



    /* Measure. */
    std::vector<void*> closures;
    for (std::vector<struct map_impl>::size_type i = 0;
         i < impls.size();
         ++i)
    {
        closures.push_back(impls[i].create_cb(argc, argv));
    }
    sleep(2); /* Coordinate with perf-stat */
    for (std::vector<struct map_impl>::size_type i = 0;
         i < impls.size();
         ++i)
    {
        void *closure = closures[i];
        impls[i].handle_events_cb(events, total_num_events, closure);
        impls[i].handle_events_cb(postremove_events, num_postremove_events, closure);
        impls[i].destroy_cb(closure);

        /* (Do not include preinsert events in our stats calculations.) */
        print_stats(impls[i].name_cb(),
                    &events[num_preinsert_events],
                    num_events);
    }

    return EXIT_SUCCESS;
}

