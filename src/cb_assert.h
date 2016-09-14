/* Copyright 2016 Daniel Kopko */
/*
 * This file is part of CB.
 *
 * CB is free software: you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * CB is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with CB.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _CB_ASSERT_H_
#define _CB_ASSERT_H_

#include "cb_misc.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


CB_INLINE void
cb_assert_impl(bool b, const char *str, const char *func, int line)
{
    /* LCOV_EXCL_START */
    if (!b)
    {
        fprintf(stderr, "Assertion \'%s\' failed. (%s():%d)\n",
                str, func, line);
        fflush(stderr);
        abort();
    }
    /* LCOV_EXCL_STOP */
}


#ifdef CB_HEAVY_ASSERT_ON
#  define cb_heavy_assert(X) cb_assert_impl(X, #X, __FUNCTION__, __LINE__)
#  ifndef CB_ASSERT_ON
#    define CB_ASSERT_ON /* Enable non-heavy asserts, too. */
#  endif
#else
#  define cb_heavy_assert(X) do { } while(0)
#endif


#ifdef CB_ASSERT_ON
#  define cb_assert(X) cb_assert_impl(X, #X, __FUNCTION__, __LINE__)
#else
#  define cb_assert(X) do { } while(0)
#endif


#endif /* ! defined _CB_ASSERT_H_*/
