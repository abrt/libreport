/*
    Copyright (C) 2009  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef CLI_REPORT_H_
#define CLI_REPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

int run_analyze_event(const char *dump_dir_name, const char *analyzer);
char *select_event_option(GList *list_options);
GList *str_to_glist(char *str, int delim);

/* Report the crash */
enum {
    CLI_REPORT_BATCH = 1 << 0,
};
int report(const char *dump_dir_name, int flags);
int collect(const char *dump_dir_name, int batch);
int run_events_chain(const char *dump_dir_name, GList *chain);

#ifdef __cplusplus
}
#endif

#endif
