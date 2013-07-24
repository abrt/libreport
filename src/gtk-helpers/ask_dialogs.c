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

typedef enum {
    ASK_YES_NO__SAVE_RESULT = 1 << 0,

    /*
     * ASK_YES_NO__YESFOREVER means that you can reply with "Yes", "No" an "Yes
     * && Don't ask me again".
     *
     * If you check the checkbox the "No" button will be disabled and you will
     * be able to click only the "Yes" button.
     *
     * Once you answer "Yes && Don't ask me again", the dialog won't appear
     * next time you call the function and Non 0 value is returned immediately
     */
    ASK_YES_NO__YESFOREVER  = 1 << 1,
} ask_yes_no_dialog_flags;

static int run_ask_yes_no_save_generic_result_dialog(ask_yes_no_dialog_flags flags,
                                                     const char *key,
                                                     const char *message,
                                                     GtkWindow *parent)
{
    const char *ask_result = get_user_setting(key);

    if (ask_result)
    {
        const bool ret = string_to_bool(ask_result);
        if (!(flags & ASK_YES_NO__YESFOREVER))
            return ret;

        /* ASK_YES_NO__YESFOREVER */
        if (ret == false)
            /* Do you want to be asked? -> No, I don't. Do whatever you want */
            return true;

        /* CONTINUE becuase saved value is "yes" and it means 'Ask me!' */
    }

    GtkWidget *dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE,
                                               "%s", message);

    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Yes"), GTK_RESPONSE_YES);
    GtkWidget *no_button = gtk_dialog_add_button(GTK_DIALOG(dialog), _("_No"), GTK_RESPONSE_NO);

    gint response = GTK_RESPONSE_NO;
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(save_dialog_response), &response);

    GtkWidget *ask_yes_no_cb = gtk_check_button_new_with_label(_("Don't ask me again"));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       ask_yes_no_cb, TRUE, TRUE, 0);

    if (flags & ASK_YES_NO__YESFOREVER)
    {
        /* Don't check the box by default. If the box is checked the 'No'
         * button is disabled and we don't want to force users to click on
         * 'Yes' button. */
        g_signal_connect(ask_yes_no_cb, "toggled",
                     G_CALLBACK(on_toggle_ask_yes_no_yesforever_cb), (gpointer)no_button);
    }

    gtk_widget_show(ask_yes_no_cb);
    gtk_dialog_run(GTK_DIALOG(dialog));

    if (flags & ASK_YES_NO__YESFOREVER)
        /* the box is checked -> Don't ask me again and my response is always 'Yes' */
        set_user_setting(key, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ask_yes_no_cb)) ? "no" : "yes");
    else if (flags & ASK_YES_NO__SAVE_RESULT)
    {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ask_yes_no_cb)))
            /* the box is checked -> remember my current answer */
            set_user_setting(key, response == GTK_RESPONSE_YES ? "yes" : "no");
    }
    else /* should not happen */
        error_msg("BUG:%s:%d %s() unknown type (0x%x) of ask_yes_no dialog",
                    __FILE__, __LINE__, __func__, flags);

    gtk_widget_destroy(dialog);

    return response == GTK_RESPONSE_YES;
}

/*
 * This function is little bit confusing. Please, consider usage of
 * run_ask_yes_no_save_result_dialog()
 *
 * Function shows a dialog with 'Yes/No' buttons and a check box allowing to
 * remember the answer. The "Don't ask me again" response is stored in
 * configuration file under 'key'.
 */
int run_ask_yes_no_yesforever_dialog(const char *key, const char *message, GtkWindow *parent)
{
    return run_ask_yes_no_save_generic_result_dialog(ASK_YES_NO__YESFOREVER, key, message, parent);
}

/*
 * Function runs a dialog with 'Yes/No' buttons and a check box allowing to
 * remember the answer. The answer is stored in configuration file under
 * 'key'.
 */
int run_ask_yes_no_save_result_dialog(const char *key, const char *message, GtkWindow *parent)
{
    return run_ask_yes_no_save_generic_result_dialog(ASK_YES_NO__SAVE_RESULT, key, message, parent);
}
