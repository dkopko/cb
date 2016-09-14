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
#ifndef _CB_MISC_H_
#define _CB_MISC_H_


#define BITS_PER_BYTE 8

#define likely(e)   __builtin_expect(!!(e), 1)
#define unlikely(e) __builtin_expect((e), 0)

#define CB_INLINE static inline

#define cb_alignof(x) (__alignof__(x))


#endif /* _CB_MISC_H_ */
