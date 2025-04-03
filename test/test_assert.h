// test_assert.h
#ifndef _TEST_ASSERT_H_
#define _TEST_ASSERT_H_

#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
#define exit_or_abort() exit(EXIT_FAILURE)
#else
#define exit_or_abort() abort()
#endif

#define test_assert(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Test assertion failed: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
            exit_or_abort(); \
        } \
    } while (0)

#endif /* _TEST_ASSERT_H_ */
