#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/queue.h>
#include "external/cycle.h"

#define ITERS 1000000

int f(void *);

struct entry {
        LIST_ENTRY(entry) entry_linkage;
};

LIST_HEAD(, entry) head = LIST_HEAD_INITIALIZER(head);


int main(int argc, char **argv)
{
    struct entry entry;
    ticks t0, t1, t2, t3;
    struct timespec ts0, ts1;
    double ticks_per_second, events_per_second;

    (void)argc;
    (void)argv;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts0);
    t0 = getticks();
    sleep(1);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
    t1 = getticks();
    ticks_per_second = (t1 - t0) / (((double)ts1.tv_sec - (double)ts0.tv_sec) +
            (((double)ts1.tv_nsec - (double)ts0.tv_nsec) / 1000000000.0));


    t0 = getticks();
    for (uint64_t i = 0; i < ITERS; ++i)
    {
        LIST_INSERT_HEAD(&head, &entry, entry_linkage);
        //f(&entry);
        //LIST_REMOVE(&entry, entry_linkage);
    }
    t1 = getticks();

    t2 = getticks();
    for (uint64_t i = 0; i < ITERS; ++i)
    {
        f(&entry);
    }
    t3 = getticks();

    //events_per_second = (ITERS / (double)(t1 - t0 - (t3 - t2))) * ticks_per_second;
    events_per_second = (ITERS / (double)(t1 - t0)) * ticks_per_second;
    printf("ticks/sec %f, events/sec: %f\n", ticks_per_second, events_per_second);

    return EXIT_SUCCESS;
}
