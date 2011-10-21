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
#ifndef INTERNAL_LIBREPORT_GTK_H_
#define INTERNAL_LIBREPORT_GTK_H_

#include <gtk/gtk.h>
#include "report.h"
#include "internal_libreport.h"

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_keyring_available;

#define make_label_autowrap_on_resize libreport_make_label_autowrap_on_resize
void make_label_autowrap_on_resize(GtkLabel *label);

#define show_events_list_dialog libreport_show_events_list_dialog
void show_events_list_dialog(GtkWindow *parent);

#define load_event_config_data_from_keyring libreport_load_event_config_data_from_keyring
void load_event_config_data_from_keyring(void);

#define find_keyring_item_id_for_event libreport_find_keyring_item_id_for_event
guint32 find_keyring_item_id_for_event(const char *event_name);

#define show_event_config_dialog libreport_show_event_config_dialog
int show_event_config_dialog(const char *event_name, GtkWindow *parent);

char * tag_url(const char* line, const char* prefix);

#ifdef __cplusplus
}
#endif

#endif
