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

int select_and_run_one_event(const char *dump_dir_name, const char *pfx, int interactive);
int run_event_chain(const char *dump_dir_name, GList *chain, int interactive);
int select_and_run_workflow(const char *dump_dir_name, GHashTable *workflows, int interactive);

#ifdef __cplusplus
}
#endif

#endif
