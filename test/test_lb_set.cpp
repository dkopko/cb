#include <stdint.h>
#include <stdlib.h>
#include <set>
#include "cb.h"
#include "cb_random.h"
#include "cb_lb_set.h"

#define DEFAULT_NUM_ENTRIES  10000
#define DEFAULT_NUM_ITERS    10000
#define DEFAULT_SEED         0


struct cb_lb_entry *
derive_lowest_entry(struct cb_lb_entry *entries, uint64_t count)
{
    struct cb_lb_entry *lowest_entry = NULL;

    /* Find lowest entry. */
    for (uint64_t i = 0; i < count; ++i)
    {
        if (!lowest_entry ||
            cb_offset_cmp(entries[i].lower_bound,
                          lowest_entry->lower_bound) == -1)
        {
            lowest_entry = &entries[i];
        }
    }

    return lowest_entry;
}


int
entrysort(const void *lhs, const void *rhs)
{
    const struct cb_lb_entry *lhs_e = (const struct cb_lb_entry *)lhs;
    const struct cb_lb_entry *rhs_e = (const struct cb_lb_entry *)rhs;

    return cb_offset_cmp(lhs_e->lower_bound, rhs_e->lower_bound);
}


int
main(int argc, char **argv)
{
    uint64_t num_entries = DEFAULT_NUM_ENTRIES;
    uint64_t num_iters   = DEFAULT_NUM_ITERS;
    uint64_t seed        = DEFAULT_SEED;
    struct cb_lb_set   lb_set;
    struct cb_lb_entry *entries;
    struct cb_lb_entry *lowest_entry = NULL;
    struct cb_lb_entry *lowest_entry_check = NULL;
    struct cb_random_state rs;
    std::set<cb_offset_t> known_offsets;

    (void)argc;
    (void)argv;

    //cb_lb_set_debug();
    //return EXIT_SUCCESS;

    /* Initialize lower-bound set.*/
    cb_lb_set_init(&lb_set);

    /* Initialize PRNG. */
    cb_random_state_init(&rs, seed);

    /* Allocate entries. */
    entries = (struct cb_lb_entry *)malloc(num_entries *
                                           sizeof(struct cb_lb_entry));
    if (!entries)
        return EXIT_FAILURE;

    /* Set initial lower bounds. */
    for (uint64_t i = 0; i < num_entries; ++i)
    {
        cb_offset_t x;

        /* Create only not-yet-existing lower-bounds. */
        do
        {
            x = cb_random_next_range(&rs, CB_OFFSET_MAX / 2);
        } while (known_offsets.find(x) != known_offsets.end());

        entries[i].lower_bound = x;
        known_offsets.insert(entries[i].lower_bound);
    }

    /* Sort all entries, so that we will add them in increasing order. */
    qsort(entries, num_entries, sizeof(entries[0]), entrysort);
    /*
    for (uint64_t i = 0; i < num_entries; ++i)
    {
        printf("ENTRY[%ju] = %ju\n",
               (uintmax_t)i,
               (uintmax_t)entries[i].lower_bound);
    }
    */

    /* Add all entries to lower-bound set. */
    for (uint64_t i = 0; i < num_entries; ++i)
        cb_lb_set_add(&lb_set, &entries[i]);

    /* Find initial lowest entry. */
    lowest_entry_check = derive_lowest_entry(entries, num_entries);
    printf("Lowest entry has offset: %ju\n",
           (uintmax_t)lowest_entry_check->lower_bound);

    /* Confirm lower-bound set reports same lower bound as we have. */
    lowest_entry = cb_lb_set_get_lowest_entry(&lb_set);
    if (lowest_entry != lowest_entry_check)
    {
        printf("Lowest-bound set reports incorrect lowest entry. "
               "reported %p %ju, known %p %ju.\n",
               lowest_entry,
               lowest_entry ? (uintmax_t)lowest_entry->lower_bound : (uintmax_t)0,
               lowest_entry_check,
               lowest_entry_check ? (uintmax_t)lowest_entry_check->lower_bound : (uintmax_t)0);
        return EXIT_FAILURE;
    }
    printf("Lowest-bound set reports correct lowest entry.\n");

    /* Run iterations where we push forward the lowest entry. */
    for (uint64_t i = 0; i < num_iters; ++i)
    {
        cb_offset_t x;

        cb_lb_set_remove(&lb_set, lowest_entry);
        known_offsets.erase(lowest_entry->lower_bound);

        do
        {
            x = cb_random_next_range(&rs, CB_OFFSET_MAX / 2);
        } while (known_offsets.find(lowest_entry->lower_bound + x) != known_offsets.end());

        lowest_entry->lower_bound += x;
        known_offsets.insert(lowest_entry->lower_bound);

        cb_lb_set_add(&lb_set, lowest_entry);

        lowest_entry_check = derive_lowest_entry(entries, num_entries);
        lowest_entry       = cb_lb_set_get_lowest_entry(&lb_set);

        if (lowest_entry != lowest_entry_check)
        {
            printf("Lowest-bound set reports incorrect lowest entry. "
                    "reported %ju, known %ju.\n",
                    (uintmax_t)lowest_entry->lower_bound,
                    (uintmax_t)lowest_entry_check->lower_bound);
            return EXIT_FAILURE;
        }
        printf("Lowest entry offset: %ju\n",
               (uintmax_t)lowest_entry->lower_bound);
    }

    printf("Test passed.\n");
    return EXIT_SUCCESS;
}

