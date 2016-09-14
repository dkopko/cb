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
#include "cb_assert.h"
#include <stdio.h>
#include <stdlib.h>


void
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
