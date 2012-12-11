/*
 *  Copyright (C) 2012  ABRT Team
 *  Copyright (C) 2012  RedHat inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <internal_libreport_gtk.h>

static void save_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    *(gint*)user_data = response_id;
}

/*
 * We don't allow users to remember 'No' answer therefore if 'Don't ask me again' box is
 * checked we have to disable 'No' button
 */
static void on_toggle_ask_yes_no_yesforever_cb(GtkToggleButton *tb, gpointer user_data)
{
    gtk_widget_set_sensitive(GTK_WIDGET(user_data), !gtk_toggle_button_get_active(tb));
}

/*
 * Function shows a dialog with 'Yes/No' buttons and a check box allowing to
 * remember the answer. The "Don't ask me again" response is stored in
 * configuration file under 'key'.
 */
int run_ask_yes_no_yesforever_dialog(const char *key, const char *message, GtkWindow *parent)
{
    const char *ask_result = get_user_setting(key);

    if (ask_result && string_to_bool(ask_result) == false)
        /* Do you want to be asked? -> No, I don't. Do whatever you want */
        return true;

    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE,
                                               "%s", message);

    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_YES, GTK_RESPONSE_YES);
    GtkWidget *no_button = gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_NO, GTK_RESPONSE_NO);

    gint response = GTK_RESPONSE_NO;
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(save_dialog_response), &response);

    GtkWidget *ask_yes_no_cb = gtk_check_button_new_with_label(_("Don't ask me again"));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       ask_yes_no_cb, TRUE, TRUE, 0);
    g_signal_connect(ask_yes_no_cb, "toggled",
                     G_CALLBACK(on_toggle_ask_yes_no_yesforever_cb), (gpointer)no_button);

    /* Don't check the box by default. If the box is checked the 'No' button is disabled and
     * we don't want to force users to click on 'Yes' button. */

    gtk_widget_show(ask_yes_no_cb);
    gtk_dialog_run(GTK_DIALOG(dialog));

    /* the box is checked -> Don't ask me again and my response is always 'Yes' */
    set_user_setting(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ask_yes_no_cb)) ? "no" : "yes");

    gtk_widget_destroy(dialog);

    return response == GTK_RESPONSE_YES;
}

