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
#include <gtk/gtk.h>
#include "internal_libreport_gtk.h"

enum
{
    COLUMN_WORKFLOW_UINAME,
    COLUMN_WORKFLOW_NAME,
    NUM_COLUMNS
};

static GtkWindow *g_parent_window;

static void create_event_config_dialog_content_cb(event_config_t *ec, gpointer content)
{
    create_event_config_dialog_content(ec, (GtkWidget *)content);
}

GtkWidget *create_workflow_config_dialog(const char *workflow_name, GtkWindow *parent)
{
    workflow_t *workflow = get_workflow(workflow_name);
    GList *events = wf_get_event_list(workflow);

    GtkWindow *parent_window = parent ? parent : g_parent_window;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                        /*title:*/ wf_get_screen_name(workflow) ? wf_get_screen_name(workflow) : workflow_name,
                        parent_window,
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_STOCK_CANCEL,
                        GTK_RESPONSE_CANCEL,
                        GTK_STOCK_OK,
                        GTK_RESPONSE_APPLY,
                        NULL);

    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, -1);

    if (parent_window != NULL)
    {
        gtk_window_set_icon_name(GTK_WINDOW(dialog),
        gtk_window_get_icon_name(parent_window));
    }

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    g_list_foreach(events, (GFunc)create_event_config_dialog_content_cb, content);

    return dialog;
}

static void add_workflow_to_liststore(gpointer key, gpointer value, gpointer user_data)
{
    config_item_info_t *info = workflow_get_config_info((workflow_t *)value);
    GtkWidget *dialog = create_workflow_config_dialog(key, g_parent_window);
    add_item_to_config_liststore(dialog, info, user_data);
}

static void load_single_event_config_foreach(event_config_t *ec, gpointer user_data)
{
    load_single_event_config_data_from_user_storage(ec);
}

static void load_events_foreach_workflow(const char *name, workflow_t *workflow, gpointer user_data)
{
    g_list_foreach(wf_get_event_list(workflow), (GFunc)load_single_event_config_foreach, NULL);
}

void show_workflow_list_dialog(GtkWindow *parent)
{
    g_parent_window = parent;
    //g_verbose = 3;
    if (g_workflow_list == NULL)
    {
        VERB1 log("workflow list is empty - reloading");
        load_workflow_config_data(WORKFLOWS_DIR);
    }

    g_hash_table_foreach(g_workflow_list, (GHFunc)load_events_foreach_workflow, NULL);

    GtkWindow *workflow_list_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_window_set_title(workflow_list_window, _("Workflow Configuration"));
    gtk_window_set_default_size(workflow_list_window, 450, 400);
    gtk_window_set_position(workflow_list_window, parent ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
    if (parent != NULL)
    {
        gtk_window_set_transient_for(workflow_list_window, parent);
        // modal = parent window can't steal focus
        gtk_window_set_modal(workflow_list_window, true);
        gtk_window_set_icon_name(workflow_list_window,
            gtk_window_get_icon_name(parent));
    }

    GtkWidget *main_vbox = create_config_list_dialog(_("Workflow"), g_workflow_list, workflow_list_window, add_workflow_to_liststore, NULL, NULL);

    gtk_container_add(GTK_CONTAINER(workflow_list_window), main_vbox);

    gtk_widget_show_all(GTK_WIDGET(workflow_list_window));

}
