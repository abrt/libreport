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

#define make_label_autowrap_on_resize libreport_make_label_autowrap_on_resize
void make_label_autowrap_on_resize(GtkLabel *label);

#define show_events_list_dialog libreport_show_events_list_dialog
void show_events_list_dialog(GtkWindow *parent);

#define is_event_config_user_storage_available libreport_is_event_config_user_storage_available
bool is_event_config_user_storage_available();

#define load_single_event_config_data_from_user_storage libreport_load_single_event_config_data_from_user_storage
void load_single_event_config_data_from_user_storage(const char *event_name, event_config_t *config);

#define load_event_config_data_from_user_storage libreport_load_event_config_data_from_user_storage
void load_event_config_data_from_user_storage(GHashTable *event_config_list);

#define save_event_config_data_to_user_storage libreport_save_event_config_data_to_user_storage
void  save_event_config_data_to_user_storage(const char *event_name,
                                             const event_config_t *event_config,
                                             bool store_password);

#define show_event_config_dialog libreport_show_event_config_dialog
int show_event_config_dialog(const char *event_name, GtkWindow *parent);

char * tag_url(const char* line, const char* prefix);

#define url_token libreport_url_token
struct url_token
{
    const char *start;
    int len;
};

#define find_url_tokens libreport_find_url_tokens
GList *find_url_tokens(const char *line);

#ifdef __cplusplus
}
#endif

#endif
