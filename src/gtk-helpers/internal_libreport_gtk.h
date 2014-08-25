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
#include "problem_details_dialog.h"
#include "problem_details_widget.h"
#include "report.h"
#include "internal_libreport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct config_dialog config_dialog_t;
typedef void (* config_save_fun_t)(gpointer data, const char *event_name);

typedef struct
{
    event_option_t *option;
    GtkWidget *widget;
} option_widget_t;

#define make_label_autowrap_on_resize libreport_make_label_autowrap_on_resize
void make_label_autowrap_on_resize(GtkLabel *label);

#define show_events_list_dialog libreport_show_events_list_dialog
void show_events_list_dialog(GtkWindow *parent);

#define is_event_config_user_storage_available libreport_is_event_config_user_storage_available
bool is_event_config_user_storage_available();

#define load_single_event_config_data_from_user_storage libreport_load_single_event_config_data_from_user_storage
void load_single_event_config_data_from_user_storage(event_config_t *config);

#define load_event_config_data_from_user_storage libreport_load_event_config_data_from_user_storage
void load_event_config_data_from_user_storage(GHashTable *event_config_list);

#define save_event_config_data_to_user_storage libreport_save_event_config_data_to_user_storage
void  save_event_config_data_to_user_storage(const char *event_name,
                                             const event_config_t *event_config,
                                             bool store_password);

#define show_event_config_dialog libreport_show_event_config_dialog
int show_event_config_dialog(const char *event_name, GtkWindow *parent);

#define create_event_config_dialog_content libreport_create_event_config_dialog_content
config_dialog_t *create_event_config_dialog_content(event_config_t *event, GtkWidget *content);

//#define show_workflow_list_dialog libreport_show_workflow_list_dialog
//void show_workflow_list_dialog(GtkWindow *parent);

void save_data_from_event_config_dialog(GList *widgets, event_config_t *ec);

#define add_item_to_config_liststore libreport_add_item_to_config_liststore
void add_item_to_config_liststore(gpointer cdialog, gpointer inf, gpointer user_data);

GtkListStore *new_conf_liststore(void);
void show_config_list_dialog(GtkWindow *parent);
GtkListStore *add_events_to_liststore(GHashTable *events);
GtkListStore *add_workflows_to_liststore(GHashTable *workflows);
config_dialog_t *new_config_dialog(GtkWidget *dialog, gpointer config_data, config_save_fun_t save_fun);
void load_workflow_config_data_from_user_storage(GHashTable *workflows);

void cdialog_set_widget(config_dialog_t *cdialog, GtkWidget *widget);
GtkWidget *cdialog_get_widget(config_dialog_t *cdialog);
gpointer cdialog_get_data(config_dialog_t *cdialog);
int cdialog_run(config_dialog_t *cdialog, const char *name);

void dehydrate_config_dialog(GList *option_widgets);

char * tag_url(const char* line, const char* prefix);

#define url_token libreport_url_token
struct url_token
{
    const char *start;
    int len;
};

#define find_url_tokens libreport_find_url_tokens
GList *find_url_tokens(const char *line);


#define reload_text_to_text_view libreport_reload_text_to_text_view
void reload_text_to_text_view(GtkTextView *tv, const char *text);

/* Ask dialogs */

/*
 * This function is little bit confusing. Please, consider usage of
 * run_ask_yes_no_save_result_dialog()
 *
 * Runs a dialog with 'Yes'/'No' buttons and 'Don't ask me again' check box and
 * waits until the dialog is closed. This variant of dialog allows user to
 * click only 'Yes' button if the check box is checked and stores "no" string
 * in user settings if the check box is checked.
 *
 * Uses libreport's user settings. Don't forget to call load_user_settings()
 * before the first call of this funcion and call save_user_settings() after
 * the last call of this function.
 *
 * @param key Key under which the response is stored. Not NULL
 * @param message Displayed message. Not NULL
 * @param parent Transient parent or NULL
 * @returns Non 0 if the answer is "Yes"; otherwise 0
 */
#define run_ask_yes_no_yesforever_dialog libreport_run_ask_yes_no_yesforever_dialog
int run_ask_yes_no_yesforever_dialog(const char *key, const char *message, GtkWindow *parent);

/*
 * Runs a dialog with 'Yes'/'No' buttons and 'Don't ask me again' check box and
 * waits until the dialog is closed. This variant of dialog allows user to
 * click both of buttons if the check box is checked and stores the answer in
 * user settings if the check box is checked.
 *
 * Uses libreport's user settings. Don't forget to call load_user_settings()
 * before the first call of this funcion and call save_user_settings() after
 * the last call of this function.
 *
 * @param key Key under which the response is stored. Not NULL
 * @param message Displayed message. Not NULL
 * @param parent Transient parent or NULL
 * @returns Non 0 if the answer is "Yes"; otherwise 0
 */
#define run_ask_yes_no_save_result_dialog libreport_run_ask_yes_no_save_result_dialog
int run_ask_yes_no_save_result_dialog(const char *key, const char *message, GtkWindow *parent);

#ifdef __cplusplus
}
#endif

#endif
