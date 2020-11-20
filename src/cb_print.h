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
#ifndef _CB_PRINT_H_
#define _CB_PRINT_H_

#include "cb.h"

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int
cb_vasprintf(cb_offset_t  *dest_offset,
             struct cb   **cb,
             const char   *fmt,
             va_list       ap);

int
cb_asprintf(cb_offset_t  *dest_offset,
            struct cb   **cb,
            const char   *fmt,
            ...);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* ! defined _CB_PRINT_H_*/
