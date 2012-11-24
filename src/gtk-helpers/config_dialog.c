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
    COLUMN_UINAME,
    COLUMN_NAME,
    CONFIG_DIALOG,
    NUM_COLUMNS
};

enum
{
    TYPE_STR,
    TYPE_POINTER
};

GtkListStore *new_conf_liststore(void)
{
    /* Create data store for the list and attach it
     * COLUMN_EVENT_UINAME -> name+description
     * COLUMN_EVENT_NAME -> event name so we can retrieve it from the row
     */
    return gtk_list_store_new(NUM_COLUMNS,
                              G_TYPE_STRING, /* Event name + description */
                              G_TYPE_STRING,  /* event name */
                              G_TYPE_POINTER
                            );
}

static const void *get_column_value_from_row(GtkTreeView *treeview, int column, int type)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    const void *retval = NULL;
    if (selection)
    {
        GtkTreeIter iter;
        GtkTreeModel *store = gtk_tree_view_get_model(treeview);
        if (gtk_tree_selection_get_selected(selection, &store, &iter) == TRUE)
        {
            GValue value = { 0 };
            gtk_tree_model_get_value(store, &iter, column, &value);
            switch(type){
                case TYPE_STR:
                    retval = g_value_get_string(&value);
                    break;
                case TYPE_POINTER:
                    retval = g_value_get_pointer(&value);
            }
        }
    }
    return retval;
}

static void on_row_changed_cb(GtkTreeView *treeview, gpointer user_data)
{
    VERB1 log("activated row: '%s'", (const char*)get_column_value_from_row(treeview, COLUMN_NAME, TYPE_STR));

    const void *dialog = get_column_value_from_row(treeview, CONFIG_DIALOG, TYPE_POINTER);
    gtk_widget_set_sensitive(GTK_WIDGET(user_data), dialog != NULL);
}

static void on_configure_button_cb(GtkWidget *button, gpointer user_data)
{
    GtkTreeView *tv = (GtkTreeView *)user_data;
    const void * dialog = get_column_value_from_row(tv, CONFIG_DIALOG, TYPE_POINTER);

    if (dialog != NULL)
    {
        int result = gtk_dialog_run(GTK_DIALOG(dialog));
        if (result == GTK_RESPONSE_APPLY)
        {
            //TODO: saving!!!
            g_print("apply\n");
        }
        //else if (result == GTK_RESPONSE_CANCEL)
        //    log("log");
        gtk_widget_hide(GTK_WIDGET(dialog));
    }
}

static void on_close_list_cb(GtkWidget *button, gpointer user_data)
{
    GtkWidget *window = (GtkWidget *)user_data;
    gtk_widget_destroy(window);
}

void add_item_to_config_liststore(gpointer dialog, gpointer inf, gpointer user_data)
{
    GtkListStore *list_store = (GtkListStore *)user_data;
    config_item_info_t *info = (config_item_info_t *)inf;

    VERB1 log("adding '%s' to workflow list\n", info->screen_name);
    char *label;
    if (ci_get_screen_name(info) != NULL && ci_get_description(info) != NULL)
        label = xasprintf("<b>%s</b>\n%s",ci_get_screen_name(info), ci_get_description(info));
    else
        //if event has no xml description
        label = xasprintf("<b>%s</b>\nNo description available", ci_get_name(info));

    GtkTreeIter iter;
    gtk_list_store_append(list_store, &iter);
    gtk_list_store_set(list_store, &iter,
                      COLUMN_UINAME, label,
                      COLUMN_NAME, ci_get_name(info),
                      CONFIG_DIALOG, dialog,
                      -1);
    free(label);
}

GtkWidget *create_config_list_dialog(const char *column_label,
                                    GHashTable *items,
                                    GtkWindow *dialog,
                                    GHFunc item_to_config_info,
                                    GCallback on_config_cb,
                                    GCallback on_row_change)
{
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    /* workflow list treeview */
    GtkWidget *tv = gtk_tree_view_new();
    /* column with workflow name and description */
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* add column to tree view */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(column_label,
                                                 renderer,
                                                 "markup",
                                                 COLUMN_UINAME,
                                                 NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    g_object_set(G_OBJECT(renderer), "wrap-mode", PANGO_WRAP_WORD, NULL);
    g_object_set(G_OBJECT(renderer), "wrap-width", 440, NULL);
    gtk_tree_view_column_set_sort_column_id(column, COLUMN_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);
    /* "Please draw rows in alternating colors": */
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tv), TRUE);
    // TODO: gtk_tree_view_set_headers_visible(FALSE)? We have only one column anyway...

    /* Create data store for the list and attach it
     * COLUMN_UINAME -> name+description
     * COLUMN_NAME -> workflow name so we can retrieve it from the row
     */
    GtkListStore *list_store = new_conf_liststore();
    gtk_tree_view_set_model(GTK_TREE_VIEW(tv), GTK_TREE_MODEL(list_store));

    g_hash_table_foreach(items,
                        item_to_config_info,
                        list_store);
//TODO: can unref workflows_list_store? treeview holds one ref.

    /* Double click/Enter handler */
    //g_signal_connect(workflows_tv, "row-activated", G_CALLBACK(on_workflow_row_activated_cb), NULL);

    gtk_container_add(GTK_CONTAINER(scroll), tv);

    GtkWidget *configure_btn = gtk_button_new_with_mnemonic(_("C_onfigure"));
    gtk_widget_set_sensitive(configure_btn, false);
    g_signal_connect(configure_btn, "clicked", G_CALLBACK(on_configure_button_cb), tv);
    g_signal_connect(tv, "cursor-changed", G_CALLBACK(on_row_changed_cb), configure_btn);

    GtkWidget *close_btn = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_list_cb), dialog);

    GtkWidget *btnbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_end(GTK_BOX(btnbox), close_btn, false, false, 0);
    gtk_box_pack_end(GTK_BOX(btnbox), configure_btn, false, false, 0);

    gtk_box_pack_start(GTK_BOX(main_vbox), scroll, true, true, 10);
    gtk_box_pack_start(GTK_BOX(main_vbox), btnbox, false, false, 0);

    return main_vbox;
}
