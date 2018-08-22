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

#include "problem_details_widget.h"
#include "internal_libreport_gtk.h"
#include "internal_libreport.h"


#define EXPLICIT_ITEMS \
    CD_DUMPDIR, \
    FILENAME_TIME, \
    FILENAME_LAST_OCCURRENCE, \
    FILENAME_UID, \
    FILENAME_USERNAME, \
    FILENAME_TYPE, \
    FILENAME_COMMENT, \
    FILENAME_ANALYZER

#define ORDERED_ITEMS \
    FILENAME_EXPLOITABLE, \
    FILENAME_NOT_REPORTABLE, \
    FILENAME_REASON, \
    FILENAME_BACKTRACE, \
    FILENAME_CRASH_FUNCTION, \
    FILENAME_CMDLINE, \
    FILENAME_EXECUTABLE, \
    FILENAME_PACKAGE, \
    FILENAME_COMPONENT, \
    FILENAME_PID, \
    FILENAME_PWD, \
    FILENAME_HOSTNAME, \
    FILENAME_COUNT

static GtkCssProvider *g_provider = NULL;

static const char *items_orderlist[] = {
    ORDERED_ITEMS,
    NULL,
};

static const char *items_auto_blacklist[] = {
    EXPLICIT_ITEMS,
    ORDERED_ITEMS,
    FILENAME_PKG_NAME,
    FILENAME_PKG_VERSION,
    FILENAME_PKG_RELEASE,
    FILENAME_PKG_ARCH,
    FILENAME_PKG_EPOCH,
    NULL,
};

struct ProblemDetailsWidgetPrivate {
    gulong rows;
    problem_data_t *problem_data;
};

G_DEFINE_TYPE_WITH_PRIVATE(ProblemDetailsWidget, problem_details_widget, GTK_TYPE_GRID)

static void problem_details_widget_finalize(GObject *object);

static void load_css_style()
{
    g_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(g_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    const gchar *data = "#value {font-family: monospace;}";
    gtk_css_provider_load_from_data(g_provider, data, -1, NULL);
    g_object_unref (g_provider);
}

static void
problem_details_widget_class_init(ProblemDetailsWidgetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = problem_details_widget_finalize;
}

static void
problem_details_widget_finalize(GObject *object)
{
    ProblemDetailsWidget *self;

    self = PROBLEM_DETAILS_WIDGET(object);

    self->priv->problem_data = (void *)0xdeadbeaf;

    G_OBJECT_CLASS(problem_details_widget_parent_class)->finalize(object);
}

static gulong
problem_details_widget_append_row(ProblemDetailsWidget *self)
{
    gtk_grid_insert_row(GTK_GRID(self), self->priv->rows);
    return self->priv->rows++;
}

static void
problem_details_widget_add_single_line(ProblemDetailsWidget *self, const char *name, const char *content)
{
    if (g_provider == NULL)
        load_css_style();

    GtkWidget *label = gtk_label_new(name);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_START);

    GtkWidget *value = gtk_label_new(content);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(value), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(value), GTK_WRAP_WORD);
    gtk_widget_set_halign(value, GTK_ALIGN_START);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_widget_set_name(GTK_WIDGET(value), "value");

    gtk_widget_set_margin_start(label, 20);
    gtk_widget_set_margin_end(label, 20);

    gtk_widget_set_margin_start(value, 5);

    const gulong row = problem_details_widget_append_row(self);

    gtk_grid_attach(GTK_GRID(self), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(self), value, 1, row, 1, 1);
}

static void
problem_details_widget_add_multi_line(ProblemDetailsWidget *self, const char *name, const char *content)
{
    if (g_provider == NULL)
        load_css_style();

#if 0
    GtkWidget *value = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(value), FALSE);

    if (strcmp(name, FILENAME_COMMENT) == 0
            || strcmp(name, FILENAME_REASON) == 0)
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(value), GTK_WRAP_WORD);

    reload_text_to_text_view(GTK_TEXT_VIEW(value), content);
#else
    GtkWidget *value = gtk_label_new(content);
    gtk_widget_set_halign(value, GTK_ALIGN_START);

    if (strcmp(name, FILENAME_COMMENT) == 0
            || strcmp(name, FILENAME_REASON) == 0)
    {
        gtk_label_set_line_wrap(GTK_LABEL(value), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(value), GTK_WRAP_WORD);
        gtk_widget_set_margin_bottom(value, 12);
    }

    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
#endif

    gtk_widget_set_name(GTK_WIDGET(value), "value");

    GtkWidget *expander = gtk_expander_new(name);
    gtk_widget_set_hexpand(expander, TRUE);
    gtk_container_add(GTK_CONTAINER(expander), value);

    const gulong row = problem_details_widget_append_row(self);

    gtk_grid_attach(GTK_GRID(self), expander, 0, row, 2, 1);
}

static void
problem_details_widget_add_binary(ProblemDetailsWidget *self, const char *label, const char *path)
{
    struct stat statbuf;
    statbuf.st_size = 0;

    if (stat(path, &statbuf) != 0)
    {
        log_warning("File '%s' does not exist", path);
        return;
    }

    gchar *size = g_format_size_full((long long)statbuf.st_size, G_FORMAT_SIZE_IEC_UNITS);
    char *msg = xasprintf(_("$DATA_DIRECTORY/%s (binary file, %s)"), label, size);
    problem_details_widget_add_single_line(self, label, msg);
    free(msg);
    g_free(size);
}

static void
problem_details_widget_add_time_stamp(ProblemDetailsWidget *self, const char *label, const char *stamp)
{
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));

    const char *ret = strptime(stamp, "%s", &tm);

    if (ret == NULL || ret[0] != '\0')
        return;

    char buf[255];
    strftime(buf, sizeof(buf), "%F %T", &tm);

    problem_details_widget_add_single_line(self, label, buf);
}

static void
problem_details_widget_add_problem_item(ProblemDetailsWidget *self, const char *name, problem_item *item)
{
    if (item->flags & CD_FLAG_TXT)
    {
        if (strchr(item->content, '\n') == NULL)
            problem_details_widget_add_single_line(self, name, item->content);
        else
            problem_details_widget_add_multi_line(self, name, item->content);
    }
    else if (item->flags & CD_FLAG_BIN)
        problem_details_widget_add_binary(self, name, item->content);
    else
        log_warning("Unsupported file type");
}

/* Callback for GHashTable */
static void
problem_data_entry_to_grid_row_one_line(const char *item_name, problem_item *item, ProblemDetailsWidget *self)
{
    if (((item->flags & CD_FLAG_TXT) && (strchr(item->content, '\n') == NULL))
             && !is_in_string_list(item_name, items_auto_blacklist))
        problem_details_widget_add_single_line(self, item_name, item->content);
}

static void
problem_data_entry_to_grid_row_multi_line(const char *item_name, problem_item *item, ProblemDetailsWidget *self)
{
    if (((item->flags & CD_FLAG_TXT) && (strchr(item->content, '\n') != NULL))
            && !is_in_string_list(item_name, items_auto_blacklist))
        problem_details_widget_add_multi_line(self, item_name, item->content);
}

static void
problem_data_entry_to_grid_row_binary(const char *item_name, problem_item *item, ProblemDetailsWidget *self)
{
    if ((item->flags & CD_FLAG_BIN)
            && !is_in_string_list(item_name, items_auto_blacklist))
        problem_details_widget_add_binary(self, item_name, item->content);
}

static void
problem_details_widget_populate(ProblemDetailsWidget *self)
{
    {   /* Explicit order */
        for (const char **iter = items_orderlist; *iter; ++iter)
        {
            struct problem_item *item = problem_data_get_item_or_NULL(
                    self->priv->problem_data, *iter);

            if (item == NULL)
                continue;

            problem_details_widget_add_problem_item(self, *iter, item);
        }
    }

    { /* comment: */
        const char *dd = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_COMMENT);
        if (dd)
            problem_details_widget_add_multi_line(self, FILENAME_COMMENT, dd);
    }

    { /* First occurence: 2014-08-26 11:08 */
        const char *ts = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_TIME);
        if (ts)
            problem_details_widget_add_time_stamp(self, "first_occurence", ts);
    }

    { /* Last occurence: 2014-08-27 11:08 */
        const char *ts = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_LAST_OCCURRENCE);
        if (ts)
            problem_details_widget_add_time_stamp(self, "last_occurence", ts);
    }

    { /* User: login(UID) */
        const char *uid = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_UID);

        const char *username = problem_data_get_content_or_NULL(
                self->priv->problem_data, "username");

        char *line = NULL;
        if (uid && username)
            line = xasprintf("%s (%s)", username, uid);
        else if (!uid && !username)
            line = xstrdup("unknown user");
        else
            line = xasprintf("%s", uid ? uid : username);

        problem_details_widget_add_single_line(self, "user", line);
        free(line);
    }

    { /* Type/Analyzer: CCpp */
        const char *type = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_TYPE);
        const char *analyzer = problem_data_get_content_or_NULL(
                self->priv->problem_data, FILENAME_ANALYZER);

        char *label = NULL;
        char *line = NULL;
        if (type != NULL && analyzer != NULL)
        {
            if (strcmp(type, analyzer) != 0)
            {
                label = xstrdup("type/analyzer");
                line = xasprintf("%s/%s", type, analyzer);
            }
            else
            {
                label = xstrdup("type");
                line = xstrdup(type);
            }
        }
        else
        {
            label = xstrdup(type ? "type" : "anlyzer");
            line = xstrdup(type ? type : analyzer);
        }

        problem_details_widget_add_single_line(self, label, line);

        free(line);
        free(label);
    }

    g_hash_table_foreach(self->priv->problem_data,
            (GHFunc)problem_data_entry_to_grid_row_one_line, self);

    { /* data directory: */
        const char *dd = problem_data_get_content_or_NULL(
                self->priv->problem_data, CD_DUMPDIR);
        if (dd)
            problem_details_widget_add_single_line(self, "data_directory", dd);

        /* show binaries below the data_directory entry */
        g_hash_table_foreach(self->priv->problem_data,
            (GHFunc)problem_data_entry_to_grid_row_binary, self);
    }

    g_hash_table_foreach(self->priv->problem_data,
            (GHFunc)problem_data_entry_to_grid_row_multi_line, self);
}

static void
problem_details_widget_init(ProblemDetailsWidget *self)
{
    self->priv = problem_details_widget_get_instance_private(self);
    self->priv->rows = 0;
    self->priv->problem_data = NULL;
}

ProblemDetailsWidget *
problem_details_widget_new(problem_data_t *problem)
{
    INITIALIZE_LIBREPORT();

    GObject *object = g_object_new(TYPE_PROBLEM_DETAILS_WIDGET, NULL);
    ProblemDetailsWidget *self = PROBLEM_DETAILS_WIDGET(object);
    self->priv->problem_data = problem;

    problem_details_widget_populate(self);
    gtk_widget_show_all(GTK_WIDGET(self));

    return self;
}

