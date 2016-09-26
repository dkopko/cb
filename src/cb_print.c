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
#include "cb_print.h"


int
cb_vasprintf(cb_offset_t  *dest_offset,
             struct cb   **cb,
             const char   *fmt,
             va_list       ap)
{
    /*
     * We don't know how big the requested format string with supplied arguments
     * will render to.  However, vsnprintf() will return how much space would
     * have been used (had it been available), and we can use this to resize
     * the continuous buffer to get a contiguous area for a do-over, which must
     * then succeed.
     */

    size_t       dest_size   = cb_contiguous_write_range(*cb);
    cb_offset_t  dest_cursor = cb_cursor(*cb);
    char        *dest        = (char*)cb_at(*cb, dest_cursor);
    size_t       str_size; /* Including null terminator. */
    int ret;

try_again:
    ret = vsnprintf(dest, dest_size, fmt, ap);
    if (ret == -1)
    {
        cb_log_errno("vsnprintf() error");
        return -1;
    }

    str_size = (size_t)ret + 1;
    if (str_size > dest_size)
    {
        ret = cb_ensure_free_contiguous(cb, str_size);
        if (ret != 0)
            return -1;

        dest_size   = cb_contiguous_write_range(*cb);
        dest_cursor = cb_cursor(*cb);
        dest        = (char*)cb_at(*cb, dest_cursor);

        cb_assert(str_size <= dest_size);

        goto try_again;
    }

    *dest_offset = dest_cursor;
    return 0;
}


int
cb_asprintf(cb_offset_t  *dest_offset,
            struct cb   **cb,
            const char   *fmt,
            ...)
{
    /*
     * FIXME va_start() may allocate memory, which is undesiarble.
     * see 'man stdarg'
     */

    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = cb_vasprintf(dest_offset, cb, fmt, ap);
    va_end(ap);

    return ret;
}
