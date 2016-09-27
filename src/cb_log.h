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
#ifndef _CB_LOG_H_
#define _CB_LOG_H_


enum cb_log_level
{
    CB_LOG_DEBUG,
    CB_LOG_ERROR
};


void cb_log_impl(enum cb_log_level lvl, const char *fmt, ...);

#define cb_log(LVL, FMT, ARGS...) \
        cb_log_impl(LVL, "[cb|%s():%d] " FMT "\n", __FUNCTION__, __LINE__, \
                    ##ARGS)


#define cb_log_error(FMT, ARGS...) cb_log(CB_LOG_ERROR, FMT, ##ARGS)

#define cb_log_errno(FMT, ARGS...) cb_log(CB_LOG_ERROR, FMT " (\"%m\")", ##ARGS)

#ifdef CB_VERBOSE
#define cb_log_debug(FMT, ARGS...) cb_log(CB_LOG_DEBUG, FMT, ##ARGS)
#else
#define cb_log_debug(FMT, ARGS...) do { } while(0)
#endif


#endif /* ! defined _CB_LOG_H_*/
