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
#include <gdk/gdk.h>
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

struct config_dialog
{
    GtkWidget *dialog;
    gpointer *data;
    config_save_fun_t save_data;
};

GtkListStore *new_conf_liststore(void)
{
    /* Create data store for the list and attach it
     * COLUMN_UINAME -> name+description
     * COLUMN_NAME -> config name so we can retrieve it from the row
     */
    return gtk_list_store_new(NUM_COLUMNS,
                              G_TYPE_STRING, /* Event name + description */
                              G_TYPE_STRING,  /* event name */
                              G_TYPE_POINTER, /* dialog */
                              G_TYPE_POINTER /* option_list */
                            );
}


config_dialog_t *new_config_dialog(GtkWidget *dialog,
                                   gpointer config_data,
                                   config_save_fun_t save_fun)
{
    config_dialog_t *cdialog = (config_dialog_t *)xmalloc(sizeof(*cdialog));
    cdialog->dialog = dialog;
    cdialog->data = config_data;
    cdialog->save_data = save_fun;
    return cdialog;
}

void cdialog_set_widget(config_dialog_t *cdialog, GtkWidget *widget)
{
    //TODO destroy(cdialog-dialog) ??
    cdialog->dialog = widget;
}

GtkWidget *cdialog_get_widget(config_dialog_t *cdialog)
{
    return cdialog->dialog;
}

gpointer cdialog_get_data(config_dialog_t *cdialog)
{
    return cdialog->data;
}

int cdialog_run(config_dialog_t *cdialog, const char *name)
{
    if (cdialog == NULL || cdialog->dialog == NULL)
    {
        log_warning("There is no configurable option for: '%s'", name);
        return GTK_RESPONSE_REJECT;
    }

    const int result = gtk_dialog_run(GTK_DIALOG(cdialog->dialog));
    if (result == GTK_RESPONSE_APPLY)
    {
        if (cdialog->save_data)
            cdialog->save_data(cdialog->data, name);
    }
    else if (result == GTK_RESPONSE_CANCEL)
        log_notice("Cancelling on user request");

    gtk_widget_hide(GTK_WIDGET(cdialog->dialog));

    return result;
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

static void save_value_from_widget(gpointer data, gpointer user_data)
{
    option_widget_t *ow = (option_widget_t *)data;

    const char *val = NULL;
    switch (ow->option->eo_type)
    {
        case OPTION_TYPE_TEXT:
        case OPTION_TYPE_NUMBER:
        case OPTION_TYPE_PASSWORD:
            val = (char *)gtk_entry_get_text(GTK_ENTRY(ow->widget));
            break;
        case OPTION_TYPE_BOOL:
            val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ow->widget)) ? "yes" : "no";
            break;
        default:
            log_warning("unsupported option type");
    }

    /* gtk_entry_get_text() returns empty string for empty text value */
    /* so if value is empty and the old value is NULL then nothing has */
    /* changed and we must not set option's value */
    if (val && (val[0] != '\0' || ow->option->eo_value != NULL))
    {
        free(ow->option->eo_value);
        ow->option->eo_value = xstrdup(val);
        log_notice("saved: %s:%s", ow->option->eo_name, ow->option->eo_value);
    }
}

void dehydrate_config_dialog(GList *option_widgets)
{
    if (option_widgets != NULL)
        g_list_foreach(option_widgets, &save_value_from_widget, NULL);
}

void add_item_to_config_liststore(gpointer cdialog, gpointer inf, gpointer user_data)
{
    INITIALIZE_LIBREPORT();

    GtkListStore *list_store = (GtkListStore *)user_data;
    config_item_info_t *info = (config_item_info_t *)inf;

    log_notice("adding '%s' to workflow list\n", ci_get_screen_name(info));
    char *label;
    if (ci_get_screen_name(info) != NULL && ci_get_description(info) != NULL)
        label = xasprintf("<b>%s</b>\n%s",ci_get_screen_name(info), ci_get_description(info));
    else
        //if event has no xml description
        label = xasprintf("<b>%s</b>\n%s", _("No description available"), ci_get_name(info));

    GtkTreeIter iter;
    gtk_list_store_append(list_store, &iter);
    gtk_list_store_set(list_store, &iter,
                      COLUMN_UINAME, label,
                      COLUMN_NAME, ci_get_name(info),
                      CONFIG_DIALOG, cdialog,
                      -1);
    free(label);
}

//filters configuration - show only those with configurable options trac#881
static gboolean config_filter_func(GtkTreeModel *model,
                                   GtkTreeIter  *iter,
                                   gpointer      data)
{
  gboolean visible = FALSE;
  gpointer cdialog;

  GValue value = { 0 };
  gtk_tree_model_get_value(model, iter, CONFIG_DIALOG, &value);
  cdialog = g_value_get_pointer(&value);
  visible = (cdialog != NULL);

  return visible;
}

static void open_config_for_selected_row(GtkTreeView *tv)
{
    config_dialog_t *cdialog = (config_dialog_t *)get_column_value_from_row(tv, CONFIG_DIALOG, TYPE_POINTER);
    const char *name = (const char *)get_column_value_from_row(tv, COLUMN_NAME, TYPE_STR);

    cdialog_run(cdialog, name);
}

static gboolean on_key_press_event_cb(GtkWidget *btn, GdkEvent *event, gpointer user_data)
{
    GdkEventKey *ek = (GdkEventKey *)event;

    if (ek->keyval == GDK_KEY_Return)
    {
        GtkTreeView *tv = (GtkTreeView *)user_data;
        open_config_for_selected_row(tv);
    }

    return FALSE;
}

static gboolean on_button_press_event_cb(GtkWidget *btn, GdkEvent *event, gpointer user_data)
{
    GdkEventButton *eb = (GdkEventButton *)event;

    if (eb->type == GDK_2BUTTON_PRESS)
    {
        GtkTreeView *tv = (GtkTreeView *)user_data;
        open_config_for_selected_row(tv);
    }

    return FALSE;
}

GtkWidget *create_config_tab_content(const char *column_label,
                                      GtkListStore *store)
{
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    /* workflow list treeview */
    GtkWidget *tv = gtk_tree_view_new();
    g_signal_connect(tv, "key-press-event", G_CALLBACK(on_key_press_event_cb), tv);
    g_signal_connect(tv, "button-press-event", G_CALLBACK(on_button_press_event_cb), tv);

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

    // TODO: gtk_tree_view_set_headers_visible(FALSE)? We have only one column anyway...
    GtkTreeModel *model = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(model), config_filter_func, NULL, NULL);

    gtk_tree_view_set_model(GTK_TREE_VIEW(tv), GTK_TREE_MODEL(model));

    {   /* Selected the first row, so we do not need to call gtk_tree_view_scroll_to_cell() */
        GtkTreeIter iter;
        gtk_tree_model_get_iter_first(model, &iter);
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
        gtk_tree_selection_select_iter(selection, &iter);
    }

    gtk_container_add(GTK_CONTAINER(scroll), tv);

    gtk_box_pack_start(GTK_BOX(main_vbox), scroll, true, true, 10);
    return main_vbox;
}

static void add_config_tabs(const char *name, GtkListStore *store, gpointer nb)
{
    GtkNotebook *ntb = (GtkNotebook *)nb;

    GtkWidget *config_list_vbox = create_config_tab_content(
                                        name,
                                        store);

    gtk_notebook_append_page(ntb, config_list_vbox, gtk_label_new(name));
}

static void on_configure_cb(GtkWidget *btn, gpointer user_data)
{
    GtkNotebook *nb = (GtkNotebook *)user_data;

    guint current_page_n = gtk_notebook_get_current_page(nb);
    GtkWidget *vbox = gtk_notebook_get_nth_page(nb, current_page_n);
    GList *children = gtk_container_get_children(GTK_CONTAINER(vbox));
    GtkScrolledWindow *sw = (GtkScrolledWindow *)children->data;

    open_config_for_selected_row((GtkTreeView *)gtk_bin_get_child(GTK_BIN(sw)));
}

static void on_close_cb(GtkWidget *btn, gpointer config_list_w)
{
    GtkWidget *w = (GtkWidget *)config_list_w;
    gtk_widget_hide(w);
}

GtkWindow *create_config_list_window(GHashTable *configs, GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    // config window
    GtkWidget *window = NULL;
    if (parent != NULL)
    {
        window = gtk_dialog_new();
        gtk_window_set_icon_name(GTK_WINDOW(window), gtk_window_get_icon_name(parent));
        gtk_window_set_modal(GTK_WINDOW(window), TRUE);
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    }
    else
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_container_set_border_width(GTK_CONTAINER(window), 5);
    gtk_window_set_title(GTK_WINDOW(window), _("Configuration"));
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 400);
    gtk_window_set_position(GTK_WINDOW(window), parent
                            ? GTK_WIN_POS_CENTER_ON_PARENT
                            : GTK_WIN_POS_CENTER);


    //g_signal_connect(window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *config_nb = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(main_vbox), config_nb, 1, 1, 0);

    /* we can't use this, because we want the workflows first and hashtable
     * doesn't return the items in the order they were added
     */
    //g_hash_table_foreach(configs, (GHFunc)add_config_tabs, config_nb);

    gpointer config = g_hash_table_lookup(configs, _("Workflows"));
    if (config != NULL)
        add_config_tabs(_("Workflows"), config, config_nb);

    config = g_hash_table_lookup(configs, _("Events"));
    if (config != NULL)
        add_config_tabs(_("Events"), config, config_nb);

    //buttons
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
    GtkWidget *configure_btn = gtk_button_new_with_mnemonic(_("C_onfigure"));

    GtkWidget *close_btn = gtk_button_new_with_mnemonic(_("_Close"));
    GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    //force apply and close to have the same size
    gtk_size_group_add_widget(sg, close_btn);
    gtk_size_group_add_widget(sg, configure_btn);

    g_signal_connect(configure_btn, "clicked", (GCallback)on_configure_cb, config_nb);
    g_signal_connect(close_btn, "clicked", (GCallback)on_close_cb, window);

    gtk_box_pack_start(GTK_BOX(btn_box), close_btn, 0, 0, 5);
    gtk_box_pack_end(GTK_BOX(btn_box), configure_btn, 0, 0, 5);


    gtk_box_pack_start(GTK_BOX(main_vbox), btn_box, 0, 0, 0);
    if (parent != NULL)
    {
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(window));
        gtk_box_pack_start(GTK_BOX(content), main_vbox, /*expand*/TRUE, /*fill*/TRUE, /*padding*/0);
        gtk_widget_show_all(content);
    }
    else
        gtk_container_add(GTK_CONTAINER(window), main_vbox);

    //gtk_widget_show_all(window);
    return GTK_WINDOW(window);
}

/*  Name | vbox with the gtk_Tree
 * <String name, GtkWidget *vbox>
*/

void show_config_list_dialog(GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    GHashTable *confs = g_hash_table_new_full(
            /*hash_func*/ g_str_hash,
            /*key_equal_func:*/ g_str_equal,
            /*key_destroy_func:*/ g_free,
            /*value_destroy_func:*/ NULL);


    //TODO: free the hashtables somewhere!!
    GHashTable *events = load_event_config_data();
    load_event_config_data_from_user_storage(events);

    GHashTable *workflows = load_workflow_config_data(WORKFLOWS_DIR);
    load_workflow_config_data_from_user_storage(workflows);
    GtkListStore *workflows_store = add_workflows_to_liststore(workflows);
    g_hash_table_insert(confs, _("Workflows"), workflows_store);

    GtkListStore *events_store = add_events_to_liststore(events);
    g_hash_table_insert(confs, _("Events"), events_store);

    GtkWindow *window = create_config_list_window(confs, parent);
    gtk_widget_show_all(GTK_WIDGET(window));
}
