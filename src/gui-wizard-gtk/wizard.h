/*
    Copyright (C) 2011  ABRT Team
    Copyright (C) 2011  RedHat inc.

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
#ifndef WIZARD_H_
#define WIZARD_H_

#include "internal_libreport_gtk.h"

void create_assistant(bool expert_mode);

enum
{
    /*
     * the selected event is updated to a first event wich can be applied on
     * the current problem directory
     */
    UPDATE_SELECTED_EVENT = 1 << 0,
};

/* Loads problem's data and update GUI elements according to the data.
 *
 * @param flags Flags to alternate the update process
 */
void update_gui_state_from_problem_data(int flags);
void show_error_as_msgbox(const char *msg);


extern char *g_glade_file;
extern char *g_dump_dir_name;
extern char *g_events;
extern GList *g_auto_event_list;
extern problem_data_t *g_cd;
void problem_data_reload_from_dump_dir(void);

#endif
