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

static GtkWindow *g_event_list_window;
static GList *g_option_list = NULL;

static bool has_password_option;

enum
{
    COLUMN_EVENT_UINAME,
    COLUMN_EVENT_NAME,
    NUM_COLUMNS
};

static GtkWidget *gtk_label_new_justify_left(const gchar *label_str)
{
    GtkWidget *label = gtk_label_new(label_str);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

    gtk_widget_set_halign (label, GTK_ALIGN_START);
    /* Make some space between label and input field to the right of it: */
    gtk_widget_set_margin_start(label, 5);
    gtk_widget_set_margin_end(label, 5);

    return label;
}

GList *add_option_widget(GList *options, GtkWidget *widget, event_option_t *option)
{
    option_widget_t *ow = g_new(option_widget_t, 1);
    ow->widget = widget;
    ow->option = option;
    options = g_list_prepend(options, ow);

    return options;
}

static void on_show_pass_cb(GtkToggleButton *tb, gpointer user_data)
{
    GtkEntry *entry = (GtkEntry *)user_data;
    gtk_entry_set_visibility(entry, gtk_toggle_button_get_active(tb));
}

static void on_show_pass_store_cb(GtkToggleButton *tb, gpointer user_data)
{
    libreport_set_user_setting("store_passwords", gtk_toggle_button_get_active(tb) ? "no" : "yes");
}

static unsigned add_one_row_to_grid(GtkGrid *table)
{
    gulong rows = (gulong)g_object_get_data(G_OBJECT(table), "n-rows");
    gtk_grid_insert_row(table, rows);
    g_object_set_data(G_OBJECT(table), "n-rows", (gpointer)(rows + 1));
    return rows;
}

void save_data_from_event_config_dialog(GList *widgets, event_config_t *ec)
{
    dehydrate_config_dialog(widgets);
    const char *const store_passwords_s = libreport_get_user_setting("store_passwords");
    libreport_save_event_config_data_to_user_storage(ec_get_name(ec),
                                           ec,
                                           !(store_passwords_s && !strcmp(store_passwords_s, "no")));
}

static void save_data_from_event_dialog_name(GList *widgets, const char *name)
{
    event_config_t *ec = get_event_config(name);

    if (ec == NULL) {
        log_warning("Cannot save data from event dialog. Event '%s' could not be found", name);
        return;
    }

    save_data_from_event_config_dialog(widgets, ec);
}


static void add_option_to_table(gpointer data, gpointer user_data)
{
    event_option_t *option = data;
    GtkGrid *option_table = user_data;
    if (option->is_advanced)
        option_table = GTK_GRID(g_object_get_data(G_OBJECT(option_table), "advanced-options"));

    GtkWidget *label;
    GtkWidget *option_input;
    unsigned last_row;

    g_autofree char *option_label = NULL;
    if (option->eo_label != NULL)
        option_label = g_strdup(option->eo_label);
    else
    {
        option_label = g_strdup(option->eo_name ? option->eo_name : "");
        /* Replace '_' with ' ' */
        char *p = option_label - 1;
        while (*++p)
            if (*p == '_')
                *p = ' ';
    }

    switch (option->eo_type)
    {
        case OPTION_TYPE_TEXT:
        case OPTION_TYPE_NUMBER:
        case OPTION_TYPE_PASSWORD:
            last_row = add_one_row_to_grid(option_table);
            label = gtk_label_new_justify_left(option_label);
            gtk_grid_attach(option_table, label,
                             /*left,top:*/ 0, last_row,
                             /*width,height:*/ 1, 1);
            option_input = gtk_entry_new();
            gtk_entry_set_activates_default(GTK_ENTRY(option_input), TRUE);
            gtk_widget_set_hexpand(option_input, TRUE);
            if (option->eo_value != NULL)
                gtk_entry_set_text(GTK_ENTRY(option_input), option->eo_value);
            gtk_grid_attach(option_table, option_input,
                             /*left,top:*/ 1, last_row,
                             /*width,height:*/ 1, 1);
            g_option_list = add_option_widget(g_option_list, option_input, option);
            if (option->eo_type == OPTION_TYPE_PASSWORD)
            {
                gtk_entry_set_visibility(GTK_ENTRY(option_input), 0);
                last_row = add_one_row_to_grid(option_table);
                GtkWidget *pass_cb = gtk_check_button_new_with_label(_("Show password"));
                gtk_grid_attach(option_table, pass_cb,
                             /*left,top:*/ 1, last_row,
                             /*width,height:*/ 1, 1);
                g_signal_connect(pass_cb, "toggled", G_CALLBACK(on_show_pass_cb), option_input);
                has_password_option = true;
            }
            break;

        case OPTION_TYPE_HINT_HTML:
            label = gtk_label_new(option_label);
            gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

            gtk_widget_set_halign(label, GTK_ALIGN_START);
            gtk_widget_set_valign(label, GTK_ALIGN_START);

            libreport_make_label_autowrap_on_resize(GTK_LABEL(label));

            last_row = add_one_row_to_grid(option_table);
            gtk_grid_attach(option_table, label,
                             /*left,top:*/ 0, last_row,
                             /*width,height:*/ 2, 1);
            break;

        case OPTION_TYPE_BOOL:
            last_row = add_one_row_to_grid(option_table);
            option_input = gtk_check_button_new_with_label(option_label);
            gtk_grid_attach(option_table, option_input,
                             /*left,top:*/ 0, last_row,
                             /*width,height:*/ 2, 1);
            if (option->eo_value != NULL)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option_input),
                                    libreport_string_to_bool(option->eo_value));
            g_option_list = add_option_widget(g_option_list, option_input, option);
            break;

        default:
            //option_input = gtk_label_new_justify_left("WTF?");
            log_warning("unsupported option type");
            return;
    }

    if (option->eo_note_html)
    {
        label = gtk_label_new(option->eo_note_html);
        gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_valign(label, GTK_ALIGN_START);

        libreport_make_label_autowrap_on_resize(GTK_LABEL(label));

        last_row = add_one_row_to_grid(option_table);
        gtk_grid_attach(option_table, label,
                             /*left,top:*/ 1, last_row,
                             /*top,heigh:*/ 1, 1);
    }

}

static GtkWidget *create_event_config_grid()
{
    GtkWidget *option_table = gtk_grid_new();

    gtk_widget_set_margin_start(option_table, 5);
    gtk_widget_set_margin_end(option_table, 5);

    gtk_widget_set_margin_top(option_table, 5);
    gtk_widget_set_margin_bottom(option_table, 5);

    gtk_grid_set_row_homogeneous(GTK_GRID(option_table), FALSE);
    gtk_grid_set_column_homogeneous(GTK_GRID(option_table), FALSE);
    gtk_grid_set_row_spacing(GTK_GRID(option_table), 10);
    g_object_set_data(G_OBJECT(option_table), "n-rows", (gpointer)-1);

    gtk_widget_set_hexpand(option_table, TRUE);
    gtk_widget_set_vexpand(option_table, TRUE);
    gtk_widget_set_halign(option_table, GTK_ALIGN_FILL);
    gtk_widget_set_valign(option_table, GTK_ALIGN_FILL);

    return option_table;
}

config_dialog_t *libreport_create_event_config_dialog_content(event_config_t *event, GtkWidget *content)
{
    INITIALIZE_LIBREPORT();

    if (content == NULL)
        content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    //event_config_t *event = get_event_config(event_name);
    GtkWidget *notebook_layout = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(content), notebook_layout, TRUE, TRUE, 0);

    GtkWidget *option_table = create_event_config_grid();

    /* table to hold advanced options
     * hidden in expander which is visible only if there's at least
     * one advanced option
    */
    GtkWidget *adv_option_table = create_event_config_grid();

    g_object_set_data(G_OBJECT(option_table), "advanced-options", adv_option_table);

    has_password_option = false;
    /* it's already stored in config_dialog_t from the previous call
     * we need to set it to null so we create a new list for the actual
     * event_config
     * note: say *NO* to the global variables!
    */
    g_option_list = NULL;
    /* this fills the g_option_list, so we can use it for new_config_dialog */
    g_list_foreach(event->options, &add_option_to_table, option_table);

    /* if there is at least one password option, add checkbox to disable storing passwords */
    /* if the user storage is not available nothing is to be stored, so it is not necessary
     * to bother with an extra checkbox about storing passwords */
    if (libreport_is_event_config_user_storage_available()
            && has_password_option)
    {
        unsigned last_row = add_one_row_to_grid(GTK_GRID(option_table));
        GtkWidget *pass_store_cb = gtk_check_button_new_with_label(_("Don't store passwords"));
        gtk_grid_attach(GTK_GRID(option_table), pass_store_cb,
                /*left,top:*/ 0, last_row,
                /*width,height:*/ 1, 1);
        const char *store_passwords = libreport_get_user_setting("store_passwords");
        if (store_passwords && !strcmp(store_passwords, "no"))
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pass_store_cb), 1);
        g_signal_connect(pass_store_cb, "toggled", G_CALLBACK(on_show_pass_store_cb), NULL);
    }

    GtkWidget *option_table_lbl = gtk_label_new_with_mnemonic(_("Basic"));
    GtkWidget *option_table_scrl = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(option_table_scrl), option_table);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook_layout), option_table_scrl, option_table_lbl);

    /* add the adv_option_table to the dialog only if there is some adv option */
    if (g_list_length(gtk_container_get_children(GTK_CONTAINER(adv_option_table))) > 0)
    {
        GtkWidget *adv_option_table_lbl = gtk_label_new_with_mnemonic(_("Advanced"));
        GtkWidget *adv_option_table_scrl = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(adv_option_table_scrl), adv_option_table);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook_layout), adv_option_table_scrl, adv_option_table_lbl);
    }
    else
        /* Do not show single tab 'Basic' */
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook_layout), FALSE);

    /* add warning if secrets service is not available showing the nagging dialog
     * is considered "too heavy UI" be designers
     */
    if (!libreport_is_event_config_user_storage_available())
    {
        GtkWidget *keyring_warn_lbl =
        gtk_label_new(
          _("Secret Service is not available, your settings won't be saved!"));
        gtk_widget_set_name(keyring_warn_lbl, "keyring_warn_lbl");

        GtkCssProvider *g_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(g_provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        const gchar *data = "#keyring_warn_lbl {color: rgba(100%, 0%, 0%, 1);}";
        gtk_css_provider_load_from_data(g_provider, data, -1, NULL);
        g_object_unref (g_provider);

        gtk_box_pack_start(GTK_BOX(content), keyring_warn_lbl, false, false, 0);
    }

    gtk_widget_show_all(content); //make it all visible

    //g_option_list is filled on
    config_dialog_t *cdialog = new_config_dialog(NULL,
                                    g_option_list,
                                    (config_save_fun_t)save_data_from_event_dialog_name
                                    );

    return cdialog;
}

config_dialog_t *create_event_config_dialog(const char *event_name, GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    event_config_t *event = get_event_config(event_name);

    if(!ec_is_configurable(event))
        return NULL;

    GtkWindow *parent_window = parent ? parent : g_event_list_window;

    g_autofree char *window_title = g_strdup_printf("%s - Reporting Configuration",
            ec_get_screen_name(event) ? ec_get_screen_name(event) : event_name);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
                        window_title,
                        parent_window,
                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                        _("_Cancel"),
                        GTK_RESPONSE_CANCEL,
                        _("_OK"),
                        GTK_RESPONSE_APPLY,
                        NULL);

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);

    /* Allow resize?
     * W/o resize, e.g. upload configuration hint looks awfully
     * line wrapped.
     */
    gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 450, 310);

    if (parent_window != NULL)
    {
        gtk_window_set_icon_name(GTK_WINDOW(dialog),
                                 gtk_window_get_icon_name(parent_window));
    }

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    config_dialog_t *cdialog = libreport_create_event_config_dialog_content(event, content);
    cdialog_set_widget(cdialog, dialog);

    return cdialog;
}

static void add_event_to_liststore(gpointer key, gpointer value, gpointer list_store)
{
    config_item_info_t *info = ec_get_config_info((event_config_t *)value);
    config_dialog_t *cdialog = create_event_config_dialog(key, NULL);

    libreport_add_item_to_config_liststore(cdialog, info, list_store);
}

GtkListStore *add_events_to_liststore(GHashTable *events)
{
    GtkListStore *list_store = new_conf_liststore();
    g_hash_table_foreach(events, (GHFunc)add_event_to_liststore, list_store);

    return list_store;
}

int libreport_show_event_config_dialog(const char *event_name, GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    config_dialog_t *dialog = create_event_config_dialog(event_name, parent);
    const int result = cdialog_run(dialog, event_name);
    free(dialog);

    return result;
}

