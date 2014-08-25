/*
    Copyright (C) 2014  ABRT Team
    Copyright (C) 2014  RedHat inc.

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

#include "problem_details_dialog.h"
#include "internal_libreport_gtk.h"
#include "internal_libreport.h"

GtkWidget *
problem_details_dialog_new(problem_data_t *problem, GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
            _("Problem details"),
            parent,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            _("OK"),
            GTK_RESPONSE_NONE,
            NULL
            );

    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 600);

    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);

    ProblemDetailsWidget *details = problem_details_widget_new(problem);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_halign(scrolled, GTK_ALIGN_FILL);
    gtk_widget_set_valign(scrolled, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);

    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(details));

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), GTK_WIDGET(scrolled));

    gtk_widget_show_all(dialog);

    return dialog;
}

GtkWidget *
problem_details_dialog_new_for_dir(const char *dir, GtkWindow *parent)
{
    INITIALIZE_LIBREPORT();

    struct dump_dir *dd = dd_opendir(dir, DD_OPEN_READONLY);
    if (!dd)
        return NULL;

    problem_data_t *problem = create_problem_data_from_dump_dir(dd);
    problem_data_add_text_noteditable(problem, CD_DUMPDIR, dir);

    dd_close(dd);

    GtkWidget *dialog = problem_details_dialog_new(problem, parent);

    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(problem_data_free), problem);

    return dialog;
}

