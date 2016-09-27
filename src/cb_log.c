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

#include "cb_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>


//FIXME log threadid
void cb_log_impl(enum cb_log_level lvl, const char *fmt, ...)
{
    int old_errno;
    FILE *file;
    va_list args;

    old_errno = errno;

    file = (lvl == CB_LOG_ERROR ? stderr : stdout);
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    errno = old_errno;
}
