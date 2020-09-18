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
#include <gdk/gdkkeysyms.h>
#include "client.h"
#include "internal_libreport_gtk.h"
#include "wizard.h"
#include "search_item.h"
#include "libreport_types.h"
#include "global_configuration.h"

#define DEFAULT_WIDTH   800
#define DEFAULT_HEIGHT  500

#define FORBIDDEN_WORDS_BLACKLLIST "forbidden_words.conf"
#define FORBIDDEN_WORDS_WHITELIST "ignored_words.conf"

typedef struct event_gui_data_t
{
    char *event_name;
    GtkToggleButton *toggle_button;
} event_gui_data_t;


/* Using GHashTable as a set of file names */
/* Each table key has associated an nonzero integer and it allows us */
/* to write the following statements:                                */
/*   if(g_hash_table_lookup(g_loaded_texts, FILENAME_COMMENT)) ...   */
static GHashTable *g_loaded_texts;
static char *g_event_selected;

static pid_t g_event_child_pid = 0;
static guint g_event_source_id = 0;

static GtkCssProvider *g_provider = NULL;

static GtkNotebook *g_assistant;
static GtkWindow *g_wnd_assistant;
static GtkBox *g_box_assistant;

static GtkWidget *g_btn_stop;
static GtkWidget *g_btn_close;
static GtkWidget *g_btn_next;
static GtkWidget *g_btn_repeat;
static GtkWidget *g_btn_detail;

static GtkBox *g_box_events;
static GtkBox *g_box_workflows;
static GtkLabel *g_lbl_event_log;
static GtkTextView *g_tv_event_log;

/* List of event_gui_data's */

/* List of event_gui_data's */
static GtkContainer *g_container_details1;
static GtkContainer *g_container_details2;

static GtkLabel *g_lbl_cd_reason;
static GtkTextView *g_tv_comment;
static GtkEventBox *g_eb_comment;
static GtkCheckButton *g_cb_no_comment;
static GtkBox *g_vb_simple_details;

static GtkComboBoxText *g_cmb_reproducible;
static GtkTextView *g_tv_steps;
static GtkLabel *g_lbl_complex_details_hint;
static GtkBox *g_vb_complex_details;

static GtkWidget *g_widget_warnings_area;
static GtkBox *g_box_warning_labels;
static GtkToggleButton *g_tb_approve_bt;
static GtkButton *g_btn_add_file;

static GtkLabel *g_lbl_size;

static GtkTreeView *g_tv_details;
static GtkCellRenderer *g_tv_details_renderer_value;
static GtkTreeViewColumn *g_tv_details_col_checkbox;
//static GtkCellRenderer *g_tv_details_renderer_checkbox;
static GtkListStore *g_ls_details;

static GtkBox *g_box_buttons; //TODO: needs not be global
static GtkNotebook *g_notebook;
static GtkListStore *g_ls_sensitive_list;
static GtkTreeView *g_tv_sensitive_list;
static GtkTreeSelection *g_tv_sensitive_sel;
static GtkRadioButton *g_rb_forbidden_words;
static GtkRadioButton *g_rb_custom_search;
static GtkExpander *g_exp_search;
static gulong g_tv_sensitive_sel_hndlr;
static gboolean g_warning_issued;

static GtkWidget *g_report_stack;
static GtkWidget *g_report_status_box;
static GtkWidget *g_report_warning_box;
static GtkSpinner *g_spinner_event_log;
static GtkImage *g_img_process_fail;
static GtkWidget *g_report_warning_label_box;

static GtkExpander *g_exp_report_log;

static GtkWidget *g_top_most_window;

static void add_workflow_buttons(GtkBox *box, GHashTable *workflows, GCallback func);
static void set_auto_event_chain(GtkButton *button, gpointer user_data);
static void start_event_run(const char *event_name);

static int tv_details_shown = 0;

static GtkWidget *g_sens_ticket;
static GtkToggleButton *g_sens_ticket_cb;

enum {
    PRIV_WARN_SHOW_BTN      = 0x01,
    PRIV_WARN_HIDE_BTN      = 0x02,
    PRIV_WARN_SHOW_MSG      = 0x04,
    PRIV_WARN_HIDE_MSG      = 0x08,
    PRIV_WARN_BTN_CHECKED   = 0x10,
    PRIV_WARN_BTN_UNCHECKED = 0x20,
};

static void private_ticket_creation_warning(int flags);
static void update_private_ticket_creation_warning_for_selected_event(void);

enum
{
    /* Note: need to update types in
     * gtk_list_store_new(DETAIL_NUM_COLUMNS, TYPE1, TYPE2...)
     * if you change these:
     */
    DETAIL_COLUMN_CHECKBOX,
    DETAIL_COLUMN_NAME,
    DETAIL_COLUMN_VALUE,
    DETAIL_NUM_COLUMNS,
};

/* Search in bt */
static guint g_timeout = 0;
static GtkEntry *g_search_entry_bt;
static const gchar *g_search_text;
static search_item_t *g_current_highlighted_word;

enum
{
    SEARCH_COLUMN_FILE,
    SEARCH_COLUMN_TEXT,
    SEARCH_COLUMN_ITEM,
};

static GtkBuilder *g_builder;

/* THE PAGE FLOW
 * page_0: introduction/summary
 * page_1: user comments
 * page_2: event selection
 * page_3: backtrace editor
 * page_4: summary
 * page_5: reporting progress
 * page_6: finished
 */
enum {
    PAGENO_SUMMARY,        // 0
    PAGENO_EVENT_SELECTOR, // 1
    PAGENO_EDIT_COMMENT,   // 2
    PAGENO_EDIT_ELEMENTS,  // 3
    PAGENO_REVIEW_DATA,    // 4
    PAGENO_EVENT_PROGRESS, // 5
    PAGENO_EVENT_DONE,     // 6
    PAGENO_NOT_SHOWN,      // 7
    NUM_PAGES              // 8
};

/* Use of arrays (instead of, say, #defines to C strings)
 * allows cheaper page_obj_t->name == PAGE_FOO comparisons
 * instead of strcmp.
 */
static const gchar PAGE_SUMMARY[]        = "page_0";
static const gchar PAGE_EVENT_SELECTOR[] = "page_1";
static const gchar PAGE_EDIT_COMMENT[]   = "page_2";
static const gchar PAGE_EDIT_ELEMENTS[]  = "page_3";
static const gchar PAGE_REVIEW_DATA[]    = "page_4";
static const gchar PAGE_EVENT_PROGRESS[] = "page_5";
static const gchar PAGE_EVENT_DONE[]     = "page_6";
static const gchar PAGE_NOT_SHOWN[]      = "page_7";

static const gchar *const page_names[] =
{
    PAGE_SUMMARY,
    PAGE_EVENT_SELECTOR,
    PAGE_EDIT_COMMENT,
    PAGE_EDIT_ELEMENTS,
    PAGE_REVIEW_DATA,
    PAGE_EVENT_PROGRESS,
    PAGE_EVENT_DONE,
    PAGE_NOT_SHOWN,
    NULL
};

#define SENSITIVE_DATA_WARN "sensitive_data_warning"
#define SENSITIVE_DATA_WARN_MSG "sensitive_data_warning_message"
#define SENSITIVE_LIST "ls_sensitive_words"
static const gchar *misc_widgets[] =
{
    SENSITIVE_DATA_WARN,
    SENSITIVE_LIST,
    NULL
};

typedef struct
{
    const gchar *name;
    const gchar *title;
    GtkWidget *page_widget;
} page_obj_t;

static page_obj_t pages[NUM_PAGES];

/* Utility functions */

static void clear_warnings(void);
static void show_warnings(void);
static void add_warning(const char   *warning,
                        GtkContainer *container);
static bool check_minimal_bt_rating(const char  *event_name,
                                    char       **warning);
static char *get_next_processed_event(GList **events_list);
static void on_next_btn_cb(GtkWidget *btn, gpointer user_data);

/* wizard.glade file as a string WIZARD_GLADE_CONTENTS: */
#include "wizard_glade.c"

static GtkBuilder *make_builder()
{
    GError *error = NULL;
    GtkBuilder *builder = gtk_builder_new();

    /* load additional widgets from glade */
    gtk_builder_add_objects_from_string(builder,
            WIZARD_GLADE_CONTENTS, sizeof(WIZARD_GLADE_CONTENTS) - 1,
            (gchar**)misc_widgets,
            &error);
    if (error != NULL)
    {
        error_msg_and_die("Error loading glade data: %s", error->message);
    }

    /* Load pages from internal string */
    gtk_builder_add_objects_from_string(builder,
            WIZARD_GLADE_CONTENTS, sizeof(WIZARD_GLADE_CONTENTS) - 1,
            (gchar**)page_names,
            &error);
    if (error != NULL)
    {
        error_msg_and_die("Error loading glade data: %s", error->message);
    }

    return builder;
}

static void load_css_style()
{
    g_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(g_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    const gchar *data = "#green_color {color: rgba(0%, 50%, 0%, 1);}\
                         #red_color {color: rgba(100%, 0%, 0%, 1);}\
                         #tev, #g_tv_event_log {font-family: monospace;}\
                         #g_eb_comment {color: #CC3333;}";
    gtk_css_provider_load_from_data(g_provider, data, -1, NULL);
    g_object_unref (g_provider);
}

static void wrap_fixer(GtkWidget *widget, gpointer data_unused)
{
    if (GTK_IS_CONTAINER(widget))
    {
        gtk_container_foreach((GtkContainer*)widget, wrap_fixer, NULL);
        return;
    }
    if (GTK_IS_LABEL(widget))
    {
        GtkLabel *label = (GtkLabel*)widget;
        if (gtk_label_get_line_wrap(label)
          && (gtk_widget_get_halign(widget) == GTK_ALIGN_START)
          && (gtk_widget_get_margin_top(widget) == 0)
          && (gtk_widget_get_margin_bottom(widget) == 0)
        ) {
            libreport_make_label_autowrap_on_resize(label);
            return;
        }
    }
}

static void fix_all_wrapped_labels(GtkWidget *widget)
{
    wrap_fixer(widget, NULL);
}

static void remove_child_widget(GtkWidget *widget, gpointer unused)
{
    /* Destroy will safely remove it and free the memory
     * if there are no refs left
     */
    gtk_widget_destroy(widget);
}


static void update_window_title(void)
{
    /* prgname can be null according to gtk documentation */
    const char *prgname = g_get_prgname();
    const char *reason = problem_data_get_content_or_NULL(g_cd, FILENAME_REASON);
    g_autofree char *title = g_strdup_printf("%s - %s", (reason ? reason : g_dump_dir_name),
            (prgname ? prgname : "report"));
    gtk_window_set_title(g_wnd_assistant, title);
}

static bool ask_continue_before_steal(const char *base_dir, const char *dump_dir)
{
    g_autofree char *msg = g_strdup_printf(
            _("Need writable directory, but '%s' is not writable."
              " Move it to '%s' and operate on the moved data?"),
            dump_dir, base_dir);
    const bool response = libreport_run_ask_yes_no_yesforever_dialog("ask_steal_dir", msg, GTK_WINDOW(g_wnd_assistant));

    return response;
}

struct dump_dir *wizard_open_directory_for_writing(const char *dump_dir_name)
{
    struct dump_dir *dd = libreport_open_directory_for_writing(dump_dir_name,
                                                     ask_continue_before_steal);

    if (dd && strcmp(g_dump_dir_name, dd->dd_dirname) != 0)
    {
        free(g_dump_dir_name);
        g_dump_dir_name = g_strdup(dd->dd_dirname);
        update_window_title();
    }

    return dd;
}

void show_error_as_msgbox(const char *msg)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING,
                GTK_BUTTONS_CLOSE,
                "%s", msg
    );
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void load_text_to_text_view(GtkTextView *tv, const char *name)
{
    /* Add to set of loaded files */
    /* a key_destroy_func() is provided therefore if the key for name already exists */
    /* a result of g_strdup() is freed */
    g_hash_table_insert(g_loaded_texts, (gpointer)g_strdup(name), (gpointer)1);

    const char *str = g_cd ? problem_data_get_content_or_NULL(g_cd, name) : NULL;
    /* Bad: will choke at any text with non-Unicode parts: */
    /* gtk_text_buffer_set_text(tb, (str ? str : ""), -1);*/
    /* Start torturing ourself instead: */

    libreport_reload_text_to_text_view(tv, str);
}

static gchar *get_malloced_string_from_text_view(GtkTextView *tv)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tv);
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void save_text_if_changed(const char *name, const char *new_value)
{
    /* a text value can't be change if the file is not loaded */
    /* returns NULL if the name is not found; otherwise nonzero */
    if (!g_hash_table_lookup(g_loaded_texts, name))
        return;

    const char *old_value = NULL;
    if (g_cd)
        old_value = problem_data_get_content_or_NULL(g_cd, name);
    if (!old_value)
        old_value = "";
    if (strcmp(new_value, old_value) != 0)
    {
        /* If the dump directory cannot be opened for writing, an error dialogue
         * will pop up because libreport_g_custom_logger is set to &show_error_as_msgbox.*/
        struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
        if (dd)
            /* If this operation fails, an error dialogue will pop up because
             * libreport_g_custom_logger is set to &show_error_as_msgbox.*/
            dd_save_text(dd, name, new_value);

        dd_close(dd);
    }
}

static void save_text_from_text_view(GtkTextView *tv, const char *name)
{
    g_autofree char *new_str = get_malloced_string_from_text_view(tv);
    save_text_if_changed(name, new_str);
}

static void append_to_textview(GtkTextView *tv, const char *str)
{
    GtkTextBuffer *tb = gtk_text_view_get_buffer(tv);

    /* Ensure we insert text at the end */
    GtkTextIter text_iter;
    gtk_text_buffer_get_end_iter(tb, &text_iter);
    gtk_text_buffer_place_cursor(tb, &text_iter);

    /* Deal with possible broken Unicode */
    const gchar *end;
    while (!g_utf8_validate(str, -1, &end))
    {
        gtk_text_buffer_insert_at_cursor(tb, str, end - str);
        char buf[8];
        unsigned len = snprintf(buf, sizeof(buf), "<%02X>", (unsigned char)*end);
        gtk_text_buffer_insert_at_cursor(tb, buf, len);
        str = end + 1;
    }

    gtk_text_buffer_get_end_iter(tb, &text_iter);

    const char *last = str;
    GList *urls = libreport_find_url_tokens(str);
    for (GList *u = urls; u; u = g_list_next(u))
    {
        const struct libreport_url_token *const t = (struct libreport_url_token *)u->data;
        if (last < t->start)
            gtk_text_buffer_insert(tb, &text_iter, last, t->start - last);

        GtkTextTag *tag;
        tag = gtk_text_buffer_create_tag(tb, NULL, "foreground", "blue",
                                         "underline", PANGO_UNDERLINE_SINGLE, NULL);
        g_autofree char *url = g_strndup(t->start, t->len);
        g_object_set_data(G_OBJECT(tag), "url", url);

        gtk_text_buffer_insert_with_tags(tb, &text_iter, url, -1, tag, NULL);

        last = t->start + t->len;
    }

    g_list_free_full(urls, g_free);

    if (last[0] != '\0')
        gtk_text_buffer_insert(tb, &text_iter, last, strlen(last));

    /* Scroll so that the end of the log is visible */
    gtk_text_view_scroll_to_iter(tv, &text_iter,
                /*within_margin:*/ 0.0, /*use_align:*/ FALSE,
                /*xalign:*/ 0, /*yalign:*/ 0);
}

/* Looks at all tags covering the position of iter in the text view,
 * and if one of them is a link, follow it by showing the page identified
 * by the data attached to it.
 */
static void open_browse_if_link(GtkWidget *text_view, GtkTextIter *iter)
{
    GSList *tags = NULL, *tagp = NULL;

    tags = gtk_text_iter_get_tags (iter);
    for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
    {
        GtkTextTag *tag = tagp->data;
        const char *url = g_object_get_data (G_OBJECT (tag), "url");

        if (url != 0)
        {
            /* http://techbase.kde.org/KDE_System_Administration/Environment_Variables#KDE_FULL_SESSION */
            if (getenv("KDE_FULL_SESSION") != NULL)
            {
                gint exitcode;
                gchar *arg[3];
                /* kde-open is from kdebase-runtime, it should be there. */
                arg[0] = (char *) "kde-open";
                arg[1] = (char *) url;
                arg[2] = NULL;

                const gboolean spawn_ret = g_spawn_sync(NULL, arg, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                 NULL, NULL, NULL, NULL, &exitcode, NULL);

                if (spawn_ret)
                    break;
            }

            GError *error = NULL;
#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 22) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 22 && GTK_MICRO_VERSION < 5))
            if (!gtk_show_uri(/* use default screen */ NULL, url, GDK_CURRENT_TIME, &error))
#else
            if (!gtk_show_uri_on_window(NULL, url, GDK_CURRENT_TIME, &error))
#endif
                error_msg("Can't open url '%s': %s", url, error->message);

            break;
        }
    }

    if (tags)
        g_slist_free (tags);
}

/* Links can be activated by pressing Enter.
 */
static gboolean key_press_event(GtkWidget *text_view, GdkEventKey *event)
{
    GtkTextIter iter;
    GtkTextBuffer *buffer;

    switch (event->keyval)
    {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
            gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                    gtk_text_buffer_get_insert(buffer));
            open_browse_if_link(text_view, &iter);
            break;

        default:
            break;
    }

    return FALSE;
}

/* Links can also be activated by clicking.
 */
static gboolean event_after(GtkWidget *text_view, GdkEvent *ev)
{
    GtkTextIter start, end, iter;
    GtkTextBuffer *buffer;
    GdkEventButton *event;
    gint x, y;

    if (ev->type != GDK_BUTTON_RELEASE)
        return FALSE;

    event = (GdkEventButton *)ev;

    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* we shouldn't follow a link if the user has selected something */
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    if (gtk_text_iter_get_offset(&start) != gtk_text_iter_get_offset(&end))
        return FALSE;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW (text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          event->x, event->y, &x, &y);

    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW (text_view), &iter, x, y);

    open_browse_if_link(text_view, &iter);

    return FALSE;
}

static gboolean hovering_over_link = FALSE;
static GdkCursor *hand_cursor = NULL;
static GdkCursor *regular_cursor = NULL;

/* Looks at all tags covering the position (x, y) in the text view,
 * and if one of them is a link, change the cursor to the "hands" cursor
 * typically used by web browsers.
 */
static void set_cursor_if_appropriate(GtkTextView *text_view,
                                      gint x,
                                      gint y)
{
    GSList *tags = NULL, *tagp = NULL;
    GtkTextIter iter;
    gboolean hovering = FALSE;

    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    tags = gtk_text_iter_get_tags(&iter);
    for (tagp = tags; tagp != NULL; tagp = tagp->next)
    {
        GtkTextTag *tag = tagp->data;
        gpointer url = g_object_get_data(G_OBJECT (tag), "url");

        if (url != 0)
        {
            hovering = TRUE;
            break;
        }
    }

    if (hovering != hovering_over_link)
    {
        hovering_over_link = hovering;

        if (hovering_over_link)
            gdk_window_set_cursor(gtk_text_view_get_window(text_view, GTK_TEXT_WINDOW_TEXT), hand_cursor);
        else
            gdk_window_set_cursor(gtk_text_view_get_window(text_view, GTK_TEXT_WINDOW_TEXT), regular_cursor);
    }

    if (tags)
        g_slist_free (tags);
}


/* Update the cursor image if the pointer moved.
 */
static gboolean motion_notify_event(GtkWidget *text_view, GdkEventMotion *event)
{
    gint x, y;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          event->x, event->y, &x, &y);

    set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), x, y);
    return FALSE;
}

/* Also update the cursor image if the window becomes visible
 * (e.g. when a window covering it got iconified).
 */
static gboolean visibility_notify_event(GtkWidget *text_view, GdkEventVisibility *event)
{
    gint wx, wy, bx, by;

    GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(text_view), GTK_TEXT_WINDOW_TEXT);
    GdkSeat *display_seat = gdk_display_get_default_seat(gdk_window_get_display(win));
    GdkDevice *pointer = gdk_seat_get_pointer(display_seat);
    gdk_window_get_device_position(gtk_widget_get_window(text_view), pointer, &wx, &wy, NULL);

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          wx, wy, &bx, &by);

    set_cursor_if_appropriate(GTK_TEXT_VIEW (text_view), bx, by);

    return FALSE;
}

/* tv_details handling */

static struct problem_item *get_current_problem_item_or_NULL(GtkTreeView *tree_view, gchar **pp_item_name)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(tree_view);

    if (selection == NULL)
        return NULL;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return NULL;

    *pp_item_name = NULL;
    gtk_tree_model_get(model, &iter,
                DETAIL_COLUMN_NAME, pp_item_name,
                -1);
    if (!*pp_item_name) /* paranoia, should never happen */
        return NULL;
    struct problem_item *item = problem_data_get_item_or_NULL(g_cd, *pp_item_name);
    return item;
}

static void tv_details_row_activated(
                        GtkTreeView       *tree_view,
                        GtkTreePath       *tree_path_UNUSED,
                        GtkTreeViewColumn *column,
                        gpointer           user_data)
{
    g_autofree gchar *item_name = NULL;
    struct problem_item *item = get_current_problem_item_or_NULL(tree_view, &item_name);
    if (!item || !(item->flags & CD_FLAG_TXT))
        return;
    if (!strchr(item->content, '\n')) /* one line? */
        return;

    gint exitcode;
    g_autofree gchar *arg[3];
    arg[0] = (char *) "xdg-open";
    arg[1] = g_build_filename(g_dump_dir_name ? g_dump_dir_name : "", item_name, NULL);
    arg[2] = NULL;

    const gboolean spawn_ret = g_spawn_sync(NULL, arg, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                 NULL, NULL, NULL, NULL, &exitcode, NULL);

    if (spawn_ret == FALSE || exitcode != EXIT_SUCCESS)
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("View/edit a text file"),
            GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            NULL, NULL);
        GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *textview = gtk_text_view_new();

        gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Save"), GTK_RESPONSE_OK);
        gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);

        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
        gtk_widget_set_size_request(scrolled, 640, 480);
        gtk_widget_show(scrolled);

        /* gtk_container_add() will now automatically add a GtkViewport if the child doesn't implement GtkScrollable. */
        gtk_container_add(GTK_CONTAINER(scrolled), textview);

        gtk_widget_show(textview);

        load_text_to_text_view(GTK_TEXT_VIEW(textview), item_name);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        {
            save_text_from_text_view(GTK_TEXT_VIEW(textview), item_name);
            problem_data_reload_from_dump_dir();
            update_gui_state_from_problem_data(/* don't update selected event */ 0);
        }

        gtk_widget_destroy(textview);
        gtk_widget_destroy(scrolled);
        gtk_widget_destroy(dialog);
    }
}

/* static gboolean tv_details_select_cursor_row(
                        GtkTreeView *tree_view,
                        gboolean arg1,
                        gpointer user_data) {...} */

static void tv_details_show(
                        GtkWidget *widget_UNUSED,
                        gpointer  user_data_UNUSED)
{
    tv_details_shown = 1;
}

static void tv_details_cursor_changed(
                        GtkTreeView *tree_view,
                        gpointer     user_data_UNUSED)
{
    /* I see this being called on window "destroy" signal when the tree_view is
       not a tree view anymore (or destroyed?) causing this error msg:
       (abrt:12804): Gtk-CRITICAL **: gtk_tree_selection_get_selected: assertion `GTK_IS_TREE_SELECTION (selection)' failed
       (abrt:12804): GLib-GObject-WARNING **: invalid uninstantiatable type `(null)' in cast to `GObject'
       (abrt:12804): GLib-GObject-CRITICAL **: g_object_set: assertion `G_IS_OBJECT (object)' failed
    */
    if (!GTK_IS_TREE_VIEW(tree_view))
        return;

    g_autofree gchar *item_name = NULL;
    struct problem_item *item = get_current_problem_item_or_NULL(tree_view, &item_name);

    /* happens when closing the wizard by clicking 'X' */
    if (!item)
        return;

    gboolean editable = (item
                /* With this, copying of non-editable fields are more difficult */
                //&& (item->flags & CD_FLAG_ISEDITABLE)
                && (item->flags & CD_FLAG_TXT)
                && !strchr(item->content, '\n')
    );

    /* Allow user to select the text with mouse.
     * Has undesirable side-effect of allowing user to "edit" the text,
     * but changes aren't saved (the old text reappears as soon as user
     * leaves the field). Need to disable editing somehow.
     */
    g_object_set(G_OBJECT(g_tv_details_renderer_value),
                "editable", editable,
                NULL);
}

static void g_tv_details_checkbox_toggled(
                        GtkCellRendererToggle *cell_renderer_UNUSED,
                        gchar    *tree_path,
                        gpointer  user_data_UNUSED)
{
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(g_ls_details), &iter, tree_path))
        return;

    g_autofree gchar *item_name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                DETAIL_COLUMN_NAME, &item_name,
                -1);
    if (!item_name) /* paranoia, should never happen */
        return;
    struct problem_item *item = problem_data_get_item_or_NULL(g_cd, item_name);
    if (!item) /* paranoia */
        return;

    int cur_value;
    if (item->selected_by_user == 0)
        cur_value = item->default_by_reporter;
    else
        cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */
    if (item->allowed_by_reporter && !item->required_by_reporter)
    {
        cur_value = !cur_value;
        item->selected_by_user = cur_value * 2 - 1; /* map 0,1 to -1,1 */
        gtk_list_store_set(g_ls_details, &iter,
                DETAIL_COLUMN_CHECKBOX, cur_value,
                -1);
    }
}

static void check_event_config(const char *event_name)
{
    GList *errors = get_options_with_err_msg(event_name);
    if (errors != NULL)
    {
        g_list_free_full(errors, (GDestroyNotify)free_invalid_options);
        libreport_show_event_config_dialog(event_name, GTK_WINDOW(g_top_most_window));
        update_private_ticket_creation_warning_for_selected_event();
    }
}

static bool isdigit_str(const char *str)
{
    do
    {
        if (*str < '0' || *str > '9') return false;
        str++;
    } while (*str);
    return true;
}

static void update_reproducible_hints(void)
{
    int reproducible = gtk_combo_box_get_active(GTK_COMBO_BOX(g_cmb_reproducible));
    switch(reproducible)
    {
        case -1:
            return;

        case PROBLEM_REPRODUCIBLE_UNKNOWN:
            gtk_label_set_text(g_lbl_complex_details_hint,
                    _("Since crashes without a known reproducer can be "
                      "difficult to diagnose, please provide a comprehensive "
                      "description of the problem you have encountered."));
            break;

        case PROBLEM_REPRODUCIBLE_YES:
            gtk_label_set_text(g_lbl_complex_details_hint,
                    _("Please provide a short description of the problem and "
                      "please include the steps you have used to reproduce "
                      "the problem."));
            break;

        case PROBLEM_REPRODUCIBLE_RECURRENT:
            gtk_label_set_text(g_lbl_complex_details_hint,
                    _("Please provide the steps you have used to reproduce the "
                      "problem."));
            break;

        default:
            error_msg("BUG: %s:%s:%d: forgotten 'how reproducible' value",
                        __FILE__, __func__, __LINE__);
            break;
    }
}

struct cd_stats {
    off_t filesize;
    unsigned filecount;
};

static void save_items_from_notepad(void)
{
    gint n_pages = gtk_notebook_get_n_pages(g_notebook);
    int i = 0;

    GtkWidget *notebook_child;
    GtkTextView *tev;
    GtkWidget *tab_lbl;
    const char *item_name;

    for (i = 0; i < n_pages; i++)
    {
        notebook_child = gtk_notebook_get_nth_page(g_notebook, i);
        tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        tab_lbl = gtk_notebook_get_tab_label(g_notebook, notebook_child);
        item_name = gtk_label_get_text(GTK_LABEL(tab_lbl));
        log_info("Saving item '%s'", item_name);

        save_text_from_text_view(tev, item_name);
    }
}

static void remove_tabs_from_notebook(GtkNotebook *notebook)
{
    gint n_pages = gtk_notebook_get_n_pages(notebook);
    int ii;

    for (ii = 0; ii < n_pages; ii++)
    {
        /* removing a page changes the indices, so we always need to remove
         * page 0
        */
        gtk_notebook_remove_page(notebook, 0); //we need to always the page 0
    }

    /* Turn off the changed callback during the update */
    g_signal_handler_block(g_tv_sensitive_sel, g_tv_sensitive_sel_hndlr);

    g_current_highlighted_word = NULL;

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_sensitive_list), &iter);
    while (valid)
    {
        g_autofree char *text = NULL;
        g_autofree search_item_t *word = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(g_ls_sensitive_list), &iter,
                SEARCH_COLUMN_TEXT, &text,
                SEARCH_COLUMN_ITEM, &word,
                -1);

        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_sensitive_list), &iter);
    }
    gtk_list_store_clear(g_ls_sensitive_list);
    g_signal_handler_unblock(g_tv_sensitive_sel, g_tv_sensitive_sel_hndlr);
}

static void append_item_to_ls_details(gpointer name, gpointer value, gpointer data)
{
    if (g_provider == NULL)
        load_css_style();

    problem_item *item = (problem_item*)value;
    struct cd_stats *stats = data;
    GtkTreeIter iter;

    gtk_list_store_append(g_ls_details, &iter);
    stats->filecount++;

    //FIXME: use the human-readable problem_item_format(item) instead of item->content.
    if (item->flags & CD_FLAG_TXT)
    {
        if (item->flags & CD_FLAG_ISEDITABLE && strcmp(name, FILENAME_ANACONDA_TB) != 0)
        {
            GtkWidget *tab_lbl = gtk_label_new((char *)name);
            GtkWidget *tev = gtk_text_view_new();
            gtk_widget_set_name(GTK_WIDGET(tev), "tev");

            if (strcmp(name, FILENAME_COMMENT) == 0 || strcmp(name, FILENAME_REASON) == 0)
                gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tev), GTK_WRAP_WORD);

            load_text_to_text_view(GTK_TEXT_VIEW(tev), (char *)name);
            /* init searching */
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tev));
            /* found items background */
            gtk_text_buffer_create_tag(buf, "search_result_bg", "background", "red", NULL);
            gtk_text_buffer_create_tag(buf, "current_result_bg", "background", "green", NULL);
            GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
            gtk_container_add(GTK_CONTAINER(sw), tev);
            gtk_notebook_append_page(g_notebook, sw, tab_lbl);
        }
        stats->filesize += strlen(item->content);
        /* If not multiline... */
        if (!strchr(item->content, '\n'))
        {
            gtk_list_store_set(g_ls_details, &iter,
                              DETAIL_COLUMN_NAME, (char *)name,
                              DETAIL_COLUMN_VALUE, item->content,
                              -1);
        }
        else
        {
            gtk_list_store_set(g_ls_details, &iter,
                              DETAIL_COLUMN_NAME, (char *)name,
                              DETAIL_COLUMN_VALUE, _("(click here to view/edit)"),
                              -1);
        }
    }
    else if (item->flags & CD_FLAG_BIN)
    {
        struct stat statbuf;
        statbuf.st_size = 0;
        if (stat(item->content, &statbuf) == 0)
        {
            stats->filesize += statbuf.st_size;
            g_autofree char *msg = g_strdup_printf(
                    _("(binary file, %llu bytes)"), (long long)statbuf.st_size);
            gtk_list_store_set(g_ls_details, &iter,
                                  DETAIL_COLUMN_NAME, (char *)name,
                                  DETAIL_COLUMN_VALUE, msg,
                                  -1);
        }
    }

    int cur_value;
    if (item->selected_by_user == 0)
        cur_value = item->default_by_reporter;
    else
        cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */

    gtk_list_store_set(g_ls_details, &iter,
            DETAIL_COLUMN_CHECKBOX, cur_value,
            -1);
}

/* Based on selected reporter, update item checkboxes */
static void update_ls_details_checkboxes(const char *event_name)
{
    event_config_t *cfg = get_event_config(event_name);
    GHashTableIter iter;
    char *name;
    struct problem_item *item;
    g_hash_table_iter_init(&iter, g_cd);
    string_vector_ptr_t global_exclude = libreport_get_global_always_excluded_elements();
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&item))
    {
        /* Decide whether item is allowed, required, and what's the default */
        item->allowed_by_reporter = 1;
        if (global_exclude)
            item->allowed_by_reporter = !libreport_is_in_string_list(name, (const_string_vector_const_ptr_t)global_exclude);

        if (cfg)
        {
            if (libreport_is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_always))
                item->allowed_by_reporter = 0;
            if ((item->flags & CD_FLAG_BIN) && cfg->ec_exclude_binary_items)
                item->allowed_by_reporter = 0;
        }

        item->default_by_reporter = item->allowed_by_reporter;
        if (cfg)
        {
            if (libreport_is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_by_default))
                item->default_by_reporter = 0;
            if (libreport_is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_include_items_by_default))
                item->allowed_by_reporter = item->default_by_reporter = 1;
        }

        item->required_by_reporter = 0;
        if (cfg)
        {
            if (libreport_is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_requires_items))
                item->default_by_reporter = item->allowed_by_reporter = item->required_by_reporter = 1;
        }

        int cur_value;
        if (item->selected_by_user == 0)
            cur_value = item->default_by_reporter;
        else
            cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */

        /* Find corresponding line and update checkbox */
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_details), &iter))
        {
            do {
                g_autofree gchar *item_name = NULL;
                gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                            DETAIL_COLUMN_NAME, &item_name,
                            -1);
                if (!item_name) /* paranoia, should never happen */
                    continue;
                int differ = strcmp(name, item_name);
                if (differ)
                    continue;
                gtk_list_store_set(g_ls_details, &iter,
                        DETAIL_COLUMN_CHECKBOX, cur_value,
                        -1);
                break;
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_details), &iter));
        }
    }
}

void update_gui_state_from_problem_data(int flags)
{
    update_window_title();
    remove_tabs_from_notebook(g_notebook);

    const char *reason = problem_data_get_content_or_NULL(g_cd, FILENAME_REASON);
    const char *not_reportable = problem_data_get_content_or_NULL(g_cd,
                                                                  FILENAME_NOT_REPORTABLE);

    g_autofree char *t = g_strdup_printf("%s%s%s",
                        not_reportable ? : "",
                        not_reportable ? " " : "",
                        reason ? : _("(no description)"));

    gtk_label_set_text(g_lbl_cd_reason, t);

    gtk_list_store_clear(g_ls_details);
    struct cd_stats stats = { 0 };
    g_hash_table_foreach(g_cd, append_item_to_ls_details, &stats);
    g_autofree char *msg = g_strdup_printf(
            _("%llu bytes, %u files"), (long long)stats.filesize, stats.filecount);
    gtk_label_set_text(g_lbl_size, msg);

    load_text_to_text_view(g_tv_comment, FILENAME_COMMENT);
    load_text_to_text_view(g_tv_steps, FILENAME_REPRODUCER);

    add_workflow_buttons(g_box_workflows, g_workflow_list,
                        G_CALLBACK(set_auto_event_chain));

    /* We can't just do gtk_widget_show_all once in main:
     * We created new widgets (buttons). Need to make them visible.
     */
    gtk_widget_show_all(GTK_WIDGET(g_wnd_assistant));

    /* Update Reproducible */
    /* Try to get the old value */
    const int reproducible = get_problem_data_reproducible(g_cd);
    if (reproducible > -1)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(g_cmb_reproducible), reproducible);
        goto reproducible_done;
    }

    /* OK, no old value.
     * Try to guess the reproducibility from the number of occurrences */
    const char *count_str = problem_data_get_content_or_NULL(g_cd, FILENAME_COUNT);
    if (   count_str == NULL
        || strcmp(count_str, "0") == 0
        || strcmp(count_str, "1") == 0
        || strcmp(count_str, "2") == 0
        || !isdigit_str(count_str))
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(g_cmb_reproducible), PROBLEM_REPRODUCIBLE_UNKNOWN);
    }
    else
    {
        char *endptr;
        int count = INT_MIN;
        long cnt = g_ascii_strtoll(count_str, &endptr, 10);
        if (cnt >= INT_MIN && cnt <= INT_MAX && count_str != endptr)
            count = (int)cnt;
        else
            error_msg_and_die("expected number in range <%d, %d>: '%s'", INT_MIN, INT_MAX, count_str);
        if (count < 5)
            gtk_combo_box_set_active(GTK_COMBO_BOX(g_cmb_reproducible), PROBLEM_REPRODUCIBLE_YES);
        else
            gtk_combo_box_set_active(GTK_COMBO_BOX(g_cmb_reproducible), PROBLEM_REPRODUCIBLE_RECURRENT);
    }

reproducible_done:
    update_reproducible_hints();
}


/* start_event_run */

struct analyze_event_data
{
    struct run_event_state *run_state;
    char *event_name;
    GList *env_list;
    GIOChannel *channel;
    GString *event_log;
    int event_log_state;
    int fd;
    /*guint event_source_id;*/
};
enum {
    LOGSTATE_FIRSTLINE = 0,
    LOGSTATE_BEGLINE,
    LOGSTATE_ERRLINE,
    LOGSTATE_MIDLINE,
};

static void set_excluded_envvar(void)
{
    GString *item_list = g_string_new(NULL);
    const char *fmt = "%s";

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_details), &iter))
    {
        do {
            g_autofree gchar *item_name = NULL;
            gboolean checked = 0;
            gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                    DETAIL_COLUMN_NAME, &item_name,
                    DETAIL_COLUMN_CHECKBOX, &checked,
                    -1);
            if (!item_name) /* paranoia, should never happen */
                continue;
            if (!checked)
            {
                g_string_append_printf(item_list, fmt, item_name);
                fmt = ", %s";
            }
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_details), &iter));
    }

    g_autofree char *var = g_string_free(item_list, FALSE);
    if (var)
        g_setenv("EXCLUDE_FROM_REPORT", var, TRUE);
    else
        unsetenv("EXCLUDE_FROM_REPORT");
}

static int spawn_next_command_in_evd(struct analyze_event_data *evd)
{
    evd->env_list = export_event_config(evd->event_name);
    int r = spawn_next_command(evd->run_state, g_dump_dir_name, evd->event_name, EXECFLG_SETPGID);
    if (r >= 0)
    {
        g_event_child_pid = evd->run_state->command_pid;
    }
    else
    {
        unexport_event_config(evd->env_list);
        evd->env_list = NULL;
    }
    return r;
}

static void save_to_event_log(struct analyze_event_data *evd, const char *str)
{
    static const char delim[] = {
        [LOGSTATE_FIRSTLINE] = '>',
        [LOGSTATE_BEGLINE] = ' ',
        [LOGSTATE_ERRLINE] = '*',
    };

    while (str[0])
    {
        char *end = strchrnul(str, '\n');
        char end_char = *end;
        if (end_char == '\n')
            end++;
        switch (evd->event_log_state)
        {
            case LOGSTATE_FIRSTLINE:
            case LOGSTATE_BEGLINE:
            case LOGSTATE_ERRLINE:
                /* skip empty lines */
                if (str[0] == '\n')
                    goto next;
                g_string_append_printf(evd->event_log, "%s%c %.*s",
                        libreport_iso_date_string(NULL),
                        delim[evd->event_log_state],
                        (int)(end - str), str
                );
                break;
            case LOGSTATE_MIDLINE:
                g_string_append_printf(evd->event_log, "%.*s", (int)(end - str), str);
                break;
        }
        evd->event_log_state = LOGSTATE_MIDLINE;
        if (end_char != '\n')
            break;
        evd->event_log_state = LOGSTATE_BEGLINE;
 next:
        str = end;
    }
}

static void update_event_log_on_disk(const char *str)
{
    /* Load existing log */
    struct dump_dir *dd = dd_opendir(g_dump_dir_name, 0);
    if (!dd)
        return;
    g_autofree char *event_log = dd_load_text_ext(dd, FILENAME_EVENT_LOG,
            DD_FAIL_QUIETLY_ENOENT);

    /* Append new log part to existing log */
    unsigned len = strlen(event_log);
    if (len != 0 && event_log[len - 1] != '\n')
        event_log = libreport_append_to_malloced_string(event_log, "\n");
    event_log = libreport_append_to_malloced_string(event_log, str);

    /* Trim log according to size watermarks */
    len = strlen(event_log);
    char *new_log = event_log;
    if (len > EVENT_LOG_HIGH_WATERMARK)
    {
        new_log += len - EVENT_LOG_LOW_WATERMARK;
        new_log = strchrnul(new_log, '\n');
        if (new_log[0])
            new_log++;
    }

    /* Save */
    dd_save_text(dd, FILENAME_EVENT_LOG, new_log);
    dd_close(dd);
}

static bool cancel_event_run()
{
    if (g_event_child_pid <= 0)
        return false;

    kill(- g_event_child_pid, SIGTERM);
    return true;
}

static void on_btn_cancel_event(GtkButton *button)
{
    cancel_event_run();
}

static bool is_processing_finished()
{
    return NULL == g_auto_event_list;
}

static void hide_next_step_button()
{
    /* replace 'Forward' with 'Close' button */
    /* 1. hide next button */
    gtk_widget_hide(g_btn_next);
    /* 2. move close button to the last position */
    gtk_box_set_child_packing(g_box_buttons, g_btn_close, false, false, 5, GTK_PACK_END);
}

static void show_next_step_button()
{
    gtk_box_set_child_packing(g_box_buttons, g_btn_close, false, false, 5, GTK_PACK_START);

    gtk_widget_show(g_btn_next);
}

enum {
 TERMINATE_NOFLAGS    = 0,
 TERMINATE_WITH_RERUN = 1 << 0,
};

static void terminate_event_chain(int flags)
{
    hide_next_step_button();
    if ((flags & TERMINATE_WITH_RERUN))
        return;

    g_clear_pointer(&g_event_selected, free);

    g_list_free_full(g_auto_event_list, free);
    g_auto_event_list = NULL;
}

static void cancel_processing(GtkLabel *status_label, const char *message, int terminate_flags)
{
    PangoAttribute *attr;
    PangoAttrList *list;

    attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
    list = pango_attr_list_new();

    pango_attr_list_insert(list, attr);

    gtk_label_set_attributes(status_label, list);
    gtk_label_set_text(status_label, message ? message : _("Processing was canceled"));
    terminate_event_chain(terminate_flags);

    pango_attr_list_unref(list);
}

static void update_command_run_log(const char* message, struct analyze_event_data *evd)
{
    const bool it_is_a_dot = (message[0] == '.' && message[1] == '\0');

    if (!it_is_a_dot)
        gtk_label_set_text(g_lbl_event_log, message);

    /* Don't append new line behind single dot */
    g_autofree const char *log_msg = it_is_a_dot ? message : g_strdup_printf("%s\n", message);
    append_to_textview(g_tv_event_log, log_msg);
    save_to_event_log(evd, log_msg);
}

static void run_event_gtk_error(const char *error_line, void *param)
{
    update_command_run_log(error_line, (struct analyze_event_data *)param);
}

static char *run_event_gtk_logging(char *log_line, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;
    update_command_run_log(log_line, evd);
    return log_line;
}

static void log_request_response_communication(const char *request, const char *response, struct analyze_event_data *evd)
{
    g_autofree char *message = g_strdup_printf(response ? "%s '%s'" : "%s", request,
            response);
    update_command_run_log(message, evd);
}

static void run_event_gtk_alert(const char *msg, void *args)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "%s", msg);
    g_autofree char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    log_request_response_communication(msg, NULL, (struct analyze_event_data *)args);
}

static void gtk_entry_emit_dialog_response_ok(GtkEntry *entry, GtkDialog *dialog)
{
    /* Don't close the dialogue if the entry is empty */
    if (gtk_entry_get_text_length(entry) > 0)
        gtk_dialog_response(dialog, GTK_RESPONSE_OK);
}

static char *ask_helper(const char *msg, void *args, bool password)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_OK_CANCEL,
            "%s", msg);
    g_autofree char *tagged_msg = tag_url(msg, "\n");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);

    GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *textbox = gtk_entry_new();
    /* gtk_entry_set_editable(GTK_ENTRY(textbox), TRUE);
     * is not available in gtk3, so please use the highlevel
     * g_object_set
     */
    g_object_set(G_OBJECT(textbox), "editable", TRUE, NULL);
    g_signal_connect(textbox, "activate", G_CALLBACK(gtk_entry_emit_dialog_response_ok), dialog);

    if (password)
        gtk_entry_set_visibility(GTK_ENTRY(textbox), FALSE);

    gtk_box_pack_start(GTK_BOX(vbox), textbox, TRUE, TRUE, 0);
    gtk_widget_show(textbox);

    char *response = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
    {
        const char *text = gtk_entry_get_text(GTK_ENTRY(textbox));
        response = g_strdup(text);
    }

    gtk_widget_destroy(textbox);
    gtk_widget_destroy(dialog);

    const char *log_response = "";
    if (response)
        log_response = password ? "********" : response;

    log_request_response_communication(msg, log_response, (struct analyze_event_data *)args);
    return response ? response : g_strdup("");
}

static char *run_event_gtk_ask(const char *msg, void *args)
{
    return ask_helper(msg, args, false);
}

static int run_event_gtk_ask_yes_no(const char *msg, void *args)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "%s", msg);
    g_autofree char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);

    /* Esc -> No, Enter -> Yes */
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    const int ret = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;

    gtk_widget_destroy(dialog);

    log_request_response_communication(msg, ret ? "YES" : "NO", (struct analyze_event_data *)args);
    return ret;
}

static int run_event_gtk_ask_yes_no_yesforever(const char *key, const char *msg, void *args)
{
    const int ret = libreport_run_ask_yes_no_yesforever_dialog(key, msg, GTK_WINDOW(g_wnd_assistant));
    log_request_response_communication(msg, ret ? "YES" : "NO", (struct analyze_event_data *)args);
    return ret;
}

static int run_event_gtk_ask_yes_no_save_result(const char *key, const char *msg, void *args)
{
    const int ret = libreport_run_ask_yes_no_save_result_dialog(key, msg, GTK_WINDOW(g_wnd_assistant));
    log_request_response_communication(msg, ret ? "YES" : "NO", (struct analyze_event_data *)args);
    return ret;
}

static char *run_event_gtk_ask_password(const char *msg, void *args)
{
    return ask_helper(msg, args, true);
}

static bool event_need_review(const char *event_name)
{
    event_config_t *event_cfg = get_event_config(event_name);
    return !event_cfg || !event_cfg->ec_skip_review;
}

static gint select_next_page_no(gint current_page_no);
static void on_page_prepare(GtkNotebook *assistant, GtkWidget *page, gpointer user_data);

static void on_btn_repeat_cb(GtkButton *button)
{
    g_auto_event_list = g_list_prepend(g_auto_event_list, g_event_selected);
    g_event_selected = NULL;

    show_next_step_button();
    clear_warnings();

    const gint current_page_no = gtk_notebook_get_current_page(g_assistant);
    const int next_page_no = select_next_page_no(PAGENO_SUMMARY);
    if (current_page_no == next_page_no)
        on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, next_page_no), NULL);
    else
        gtk_notebook_set_current_page(g_assistant, next_page_no);
}

static void on_failed_event(const char *event_name)
{
   add_warning(_("Processing of the problem failed. This can have many reasons but there are three most common:\n"\
                 "\t▫ <b>network connection problems</b>\n"\
                 "\t▫ <b>corrupted problem data</b>\n"\
                 "\t▫ <b>invalid configuration</b>"),
               GTK_CONTAINER(g_report_warning_label_box));

    add_warning(_("If you want to update the configuration and try to report again, please open <b>Preferences</b> item\n"
                  "in the application menu and after applying the configuration changes click <b>Repeat</b> button."),
                GTK_CONTAINER(g_report_warning_label_box));
    gtk_widget_show(g_btn_repeat);

    gtk_widget_show(g_report_warning_label_box);
}

static bool event_requires_details(const char *event_name)
{
    event_config_t *cfg = get_event_config(event_name);
    return cfg != NULL && cfg->ec_requires_details;
}

static gboolean consume_cmd_output(GIOChannel *source, GIOCondition condition, gpointer data)
{
    struct analyze_event_data *evd = data;
    struct run_event_state *run_state = evd->run_state;

    bool stop_requested = false;
    int retval = consume_event_command_output(run_state, g_dump_dir_name);

    if (retval < 0 && errno == EAGAIN)
        /* We got all buffered data, but fd is still open. Done for now */
        return TRUE; /* "please don't remove this event (yet)" */

    /* EOF/error */

    if (WIFEXITED(run_state->process_status)
     && WEXITSTATUS(run_state->process_status) == EXIT_STOP_EVENT_RUN
    ) {
        retval = 0;
        run_state->process_status = 0;
        stop_requested = true;
        terminate_event_chain(TERMINATE_NOFLAGS);
    }

    unexport_event_config(evd->env_list);
    evd->env_list = NULL;

    /* Make sure "Cancel" button won't send anything (process is gone) */
    g_event_child_pid = -1;
    evd->run_state->command_pid = -1; /* just for consistency */

    /* Write a final message to the log */
    if (evd->event_log->len != 0 && evd->event_log->str[evd->event_log->len - 1] != '\n')
        save_to_event_log(evd, "\n");

    /* If program failed, or if it finished successfully without saying anything... */
    if (retval != 0 || evd->event_log_state == LOGSTATE_FIRSTLINE)
    {
        g_autofree char *msg = libreport_exit_status_as_string(evd->event_name,
                run_state->process_status);
        if (retval != 0)
        {
            /* If program failed, emit *error* line */
            evd->event_log_state = LOGSTATE_ERRLINE;
        }
        append_to_textview(g_tv_event_log, msg);
        save_to_event_log(evd, msg);
    }

    /* Append log to FILENAME_EVENT_LOG */
    update_event_log_on_disk(evd->event_log->str);
    g_string_erase(evd->event_log, 0, -1);
    evd->event_log_state = LOGSTATE_FIRSTLINE;

    struct dump_dir *dd = NULL;
    if (geteuid() == 0)
    {
        /* Reset mode/uig/gid to correct values for all files created by event run */
        dd = dd_opendir(g_dump_dir_name, 0);
        if (dd)
            dd_sanitize_mode_and_owner(dd);
    }

    if (retval == 0 && libreport_get_global_stop_on_not_reportable())
    {
        /* Check whether NOT_REPORTABLE element appeared. If it did, we'll stop
         * even if exit code is "success".
         */
        if (!dd) /* why? because dd may be already open by the code above */
            dd = dd_opendir(g_dump_dir_name, DD_OPEN_READONLY | DD_FAIL_QUIETLY_EACCES);
        if (!dd)
            libreport_xfunc_die();
        g_autofree char *not_reportable = dd_load_text_ext(dd, FILENAME_NOT_REPORTABLE,
                  DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE
                | DD_FAIL_QUIETLY_ENOENT
                | DD_FAIL_QUIETLY_EACCES);
        if (not_reportable)
            retval = 256;
    }
    if (dd)
        dd_close(dd);

    /* Stop if exit code is not 0, or no more commands */
    if (stop_requested
     || retval != 0
     || spawn_next_command_in_evd(evd) < 0
    ) {
        log_notice("done running event on '%s': %d", g_dump_dir_name, retval);
        append_to_textview(g_tv_event_log, "\n");

        /* Hide spinner and stop btn */
        gtk_widget_hide(GTK_WIDGET(g_spinner_event_log));
        gtk_widget_hide(g_btn_stop);
        /* Enable (un-gray out) navigation buttons */
        gtk_widget_set_sensitive(g_btn_close, true);
        gtk_widget_set_sensitive(g_btn_next, true);

        problem_data_reload_from_dump_dir();
        update_gui_state_from_problem_data(UPDATE_SELECTED_EVENT);

        if (retval != 0)
        {
            gtk_widget_show(GTK_WIDGET(g_img_process_fail));
            /* 256 means NOT_REPORTABLE */
            if (retval == 256)
                cancel_processing(g_lbl_event_log, _("Processing was interrupted because the problem is not reportable."), TERMINATE_NOFLAGS);
            else
            {
                /* We use SIGTERM to stop event processing on user's request.
                 * So SIGTERM is not a failure.
                 */
                if (retval == EXIT_CANCEL_BY_USER || WTERMSIG(run_state->process_status) == SIGTERM)
                    cancel_processing(g_lbl_event_log, /* default message */ NULL, TERMINATE_NOFLAGS);
                else
                {
                    cancel_processing(g_lbl_event_log, _("Processing failed."), TERMINATE_WITH_RERUN);
                    on_failed_event(evd->event_name);
                }
            }
        }
        else
        {
            gtk_label_set_text(g_lbl_event_log, is_processing_finished() ? _("Processing finished.")
                                                                         : _("Processing finished, please proceed to the next step."));
        }

        g_source_remove(g_event_source_id);
        g_event_source_id = 0;
        close(evd->fd);
        g_io_channel_unref(evd->channel);
        free_run_event_state(evd->run_state);
        g_string_free(evd->event_log, TRUE);
        free(evd->event_name);
        free(evd);

        /* Inform abrt-gui that it is a good idea to rescan the directory */
        kill(getppid(), SIGCHLD);

        if (is_processing_finished())
            hide_next_step_button();
        else if (retval == 0 && !libreport_g_verbose)
            on_next_btn_cb(g_btn_next, NULL);

        return FALSE; /* "please remove this event" */
    }

    /* New command was started. Continue waiting for input */

    /* Transplant cmd's output fd onto old one, so that main loop
     * is none the wiser that fd it waits on has changed
     */
    libreport_xmove_fd(evd->run_state->command_out_fd, evd->fd);
    evd->run_state->command_out_fd = evd->fd; /* just to keep it consistent */
    libreport_ndelay_on(evd->fd);

    /* Revive "Cancel" button */
    g_event_child_pid = evd->run_state->command_pid;

    return TRUE; /* "please don't remove this event (yet)" */
}

static void start_event_run(const char *event_name)
{
    /* Start event asynchronously on the dump dir
     * (synchronous run would freeze GUI until completion)
     */

    struct run_event_state *state = new_run_event_state();
    state->logging_callback = run_event_gtk_logging;
    state->error_callback = run_event_gtk_error;
    state->alert_callback = run_event_gtk_alert;
    state->ask_callback = run_event_gtk_ask;
    state->ask_yes_no_callback = run_event_gtk_ask_yes_no;
    state->ask_yes_no_yesforever_callback = run_event_gtk_ask_yes_no_yesforever;
    state->ask_yes_no_save_result_callback = run_event_gtk_ask_yes_no_save_result;
    state->ask_password_callback = run_event_gtk_ask_password;

    if (prepare_commands(state) == 0)
    {
        /* Strangely, no commands at all were found for processing the crashdump.
         * Let the user know and terminate processing.
         */
        free_run_event_state(state);

        log_warning("No processing commands specified. Processing halted.");
        g_autofree char *msg = g_strdup_printf(
                _("No commands could be found for processsing the crashdump.\n"));
        append_to_textview(g_tv_event_log, msg);

        cancel_processing(g_lbl_event_log, _("Processing failed."), TERMINATE_NOFLAGS);

        return;
    }

    struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
    dd_close(dd);
    if (!dd)
    {
        free_run_event_state(state);

        cancel_processing(g_lbl_event_log,
                _("Processing interrupted: can't continue without writable directory."),
                TERMINATE_NOFLAGS);

        return; /* user refused to steal, or write error, etc... */
    }
    if (tv_details_shown)
        set_excluded_envvar();
    GList *env_list = export_event_config(event_name);

    int retval = spawn_next_command(state, g_dump_dir_name, event_name, EXECFLG_SETPGID);
    log_notice("Return value of %s was %d", event_name, retval);
    if (retval < 0)
    {
        /* No commands were run for this event because none matching were found.
         * Acknowledge this fact and continue with processing.
         */
        assert(state->children_count == 0);
        unexport_event_config(env_list);
        free_run_event_state(state);

        log_notice("No matching actions for event '%s'; skipping.", event_name);

        g_autofree char *msg = g_strdup_printf(
                _("--- Skipping %s ---\n"
                  "No matching actions found for this event.\n\n"),
                event_name);
        append_to_textview(g_tv_event_log, msg);

        if (is_processing_finished())
            hide_next_step_button();

        return;
    }

    g_event_child_pid = state->command_pid;

    /* At least one command is needed, and we started first one.
     * Hook its output fd to the main loop.
     */
    struct analyze_event_data *evd = g_new0(struct analyze_event_data, 1);
    evd->run_state = state;
    evd->event_name = g_strdup(event_name);
    evd->env_list = env_list;
    evd->event_log = g_string_new(NULL);
    evd->fd = state->command_out_fd;

    state->logging_param = evd;
    state->error_param = evd;
    state->interaction_param = evd;

    libreport_ndelay_on(evd->fd);
    evd->channel = g_io_channel_unix_new(evd->fd);
    g_event_source_id = g_io_add_watch(evd->channel,
            G_IO_IN | G_IO_ERR | G_IO_HUP, /* need HUP to detect EOF w/o any data */
            consume_cmd_output,
            evd
    );

    gtk_label_set_text(g_lbl_event_log, _("Processing..."));
    log_notice("running event '%s' on '%s'", event_name, g_dump_dir_name);
    g_autofree char *msg = g_strdup_printf("--- Running %s ---\n", event_name);
    append_to_textview(g_tv_event_log, msg);

    /* don't bother testing if they are visible, this is faster */
    gtk_widget_hide(GTK_WIDGET(g_img_process_fail));

    gtk_widget_show(GTK_WIDGET(g_spinner_event_log));
    gtk_widget_show(g_btn_stop);
    /* Disable (gray out) navigation buttons */
    gtk_widget_set_sensitive(g_btn_close, false);
    gtk_widget_set_sensitive(g_btn_next, false);
}

/*
 * the widget is added as a child of the VBox in the warning area
 *
 */
static void add_widget_to_warning_area(GtkWidget    *widget,
                                       GtkContainer *container)
{
    g_warning_issued = true;
    gtk_container_add(container, widget);
    gtk_widget_show_all(widget);
}

/* Backtrace checkbox handling */

static void add_warning(const char   *warning,
                        GtkContainer *container)
{
    g_autofree char *label_str = g_strdup_printf("• %s", warning);
    GtkWidget *warning_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warning_lbl), label_str);

    gtk_widget_set_halign (warning_lbl, GTK_ALIGN_START);
    gtk_widget_set_valign (warning_lbl, GTK_ALIGN_END);

    gtk_label_set_justify(GTK_LABEL(warning_lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_ellipsize(GTK_LABEL(warning_lbl), PANGO_ELLIPSIZE_END);

    add_widget_to_warning_area(warning_lbl, container);
}

static void on_sensitive_ticket_clicked_cb(GtkWidget *button, gpointer user_data)
{
    libreport_set_global_create_private_ticket(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)), /*transient*/0);
}

static void on_privacy_info_btn(GtkWidget *button, gpointer user_data)
{
    if (g_event_selected == NULL)
        return;

    libreport_show_event_config_dialog(g_event_selected, GTK_WINDOW(g_top_most_window));
}

static void private_ticket_creation_warning(int flags)
{
    if (flags & PRIV_WARN_HIDE_BTN)
    {
        gtk_widget_hide(GTK_WIDGET(g_sens_ticket));
    }

    if (flags & PRIV_WARN_SHOW_BTN)
    {
        gtk_widget_show_all(GTK_WIDGET(g_sens_ticket));
        gtk_widget_show(GTK_WIDGET(g_sens_ticket));
    }

    if (flags & PRIV_WARN_BTN_UNCHECKED)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_sens_ticket_cb), FALSE);

    if (flags & PRIV_WARN_BTN_CHECKED)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_sens_ticket_cb), TRUE);

    if (flags & PRIV_WARN_HIDE_MSG)
        clear_warnings();

    if (flags & PRIV_WARN_SHOW_MSG)
    {
       add_warning(_("Possible sensitive data detected, feel free to edit the report and remove them."),
                   GTK_CONTAINER(g_box_warning_labels));
       show_warnings();
    }
}

static void add_sensitive_data_warning(void)
{
    int flags = PRIV_WARN_SHOW_MSG;

    event_config_t *cfg = get_event_config(g_event_selected);
    if (cfg != NULL && cfg->ec_supports_restricted_access)
        flags |= PRIV_WARN_SHOW_BTN | PRIV_WARN_BTN_CHECKED;

    private_ticket_creation_warning(flags);
}

static void show_warnings(void)
{
    if (g_warning_issued)
        gtk_widget_show(g_widget_warnings_area);
}

static void clear_warnings(void)
{
    /* erase all warnings */
    if (!g_warning_issued)
        return;

    gtk_widget_hide(g_widget_warnings_area);
    gtk_container_foreach(GTK_CONTAINER(g_box_warning_labels), &remove_child_widget, NULL);
    gtk_container_foreach(GTK_CONTAINER(g_report_warning_label_box), &remove_child_widget, NULL);
    gtk_widget_hide(g_report_warning_label_box);
    g_warning_issued = false;
}

static bool check_minimal_bt_rating(const char  *event_name,
                                    char       **warning)
{
    bool acceptable_rating = true;
    event_config_t *event_cfg = NULL;

    if (!event_name)
        error_msg_and_die(_("Cannot check backtrace rating because of invalid event name"));
    else if (!g_str_has_prefix(event_name, "report"))
    {
        log_info("No checks for backtrace rating because event '%s' doesn't report.", event_name);
        return acceptable_rating;
    }
    else
        event_cfg = get_event_config(event_name);

    acceptable_rating = check_problem_rating_usability(event_cfg, g_cd, warning, NULL);

    return acceptable_rating;
}

static void on_bt_approve_toggle(GtkToggleButton *togglebutton, gpointer user_data)
{
    gtk_widget_set_sensitive(g_btn_next, gtk_toggle_button_get_active(g_tb_approve_bt));
}

static void toggle_eb_comment(void)
{
    /* The page doesn't exist with report-only option */
    if (pages[PAGENO_EDIT_COMMENT].page_widget == NULL)
        return;

    bool complex_details = g_event_selected
                           && event_requires_details(g_event_selected);
    bool good = false;
    if (complex_details)
    {
        int reproducible = gtk_combo_box_get_active(GTK_COMBO_BOX(g_cmb_reproducible));
        const int comment_chars = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(g_tv_comment));
        const int steps_chars = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(g_tv_steps));
        const int steps_lines = steps_chars == 0 ? 0 : gtk_text_buffer_get_line_count(gtk_text_view_get_buffer(g_tv_steps));
        switch(reproducible)
        {
            case -1:
                VERB1 log_warning("Uninitialized 'How reproducible' combobox");
                break;

            case PROBLEM_REPRODUCIBLE_UNKNOWN:
                good = comment_chars + (steps_chars * 2) >= 20;
                break;

            case PROBLEM_REPRODUCIBLE_YES:
                good = comment_chars >= 10 && steps_lines;
                break;

            case PROBLEM_REPRODUCIBLE_RECURRENT:
                good = comment_chars >= 10 || steps_lines;
                break;

            default:
                error_msg("BUG: %s:%s:%d: forgotten 'how reproducible' value",
                            __FILE__, __func__, __LINE__);
                break;
        }
    }
    else
    {
        good = gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(g_tv_comment)) >= 10
               || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_cb_no_comment));

        /* And show the eventbox with label */
        if (good)
            gtk_widget_hide(GTK_WIDGET(g_eb_comment));
        else
            gtk_widget_show(GTK_WIDGET(g_eb_comment));
    }

    /* Allow next page only when the comment has at least 10 chars */
    gtk_widget_set_sensitive(g_btn_next, good);
}

static void on_comment_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    toggle_eb_comment();
}

static void on_no_comment_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    toggle_eb_comment();
}

static void on_steps_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    toggle_eb_comment();
}

static void on_log_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    gtk_widget_show(GTK_WIDGET(g_exp_report_log));
}

static GList *find_words_in_text_buffer(int page,
                                        GtkTextView *tev,
                                        GList *words,
                                        GList *ignore_sitem_list,
                                        GtkTextIter start_find,
                                        GtkTextIter end_find,
                                        bool case_insensitive
                                        )
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tev);
    gtk_text_buffer_set_modified(buffer, FALSE);

    GList *found_words = NULL;
    GtkTextIter start_match;
    GtkTextIter end_match;

    for (GList *w = words; w; w = g_list_next(w))
    {
        gtk_text_buffer_get_start_iter(buffer, &start_find);

        const char *search_word = w->data;
        while (search_word && search_word[0] && gtk_text_iter_forward_search(&start_find, search_word,
                    GTK_TEXT_SEARCH_TEXT_ONLY | (case_insensitive ? GTK_TEXT_SEARCH_CASE_INSENSITIVE : 0),
                    &start_match,
                    &end_match, NULL))
        {
            search_item_t *found_word = sitem_new(
                    page,
                    buffer,
                    tev,
                    start_match,
                    end_match
                );
            int offset = gtk_text_iter_get_offset(&end_match);
            gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);

            if (g_list_find_custom(ignore_sitem_list, found_word, (GCompareFunc)sitem_contains))
            {
                if (found_word)
                    free(found_word);
                // don't count the word if it's part of some of the ignored words
                continue;
            }

            found_words = g_list_prepend(found_words, found_word);
        }
    }

    return found_words;
}

static void search_item_to_list_store_item(GtkListStore *store, GtkTreeIter *new_row,
        const gchar *file_name, search_item_t *word)
{
    GtkTextIter *beg = gtk_text_iter_copy(&(word->start));
    gtk_text_iter_backward_line(beg);

    GtkTextIter *end = gtk_text_iter_copy(&(word->end));
    /* the first call moves end variable at the end of the current line */
    if (gtk_text_iter_forward_line(end))
    {
        /* the second call moves end variable at the end of the next line */
        gtk_text_iter_forward_line(end);

        /* don't include the last new which causes an empty line in the GUI list */
        gtk_text_iter_backward_char(end);
    }

    g_autofree gchar *tmp = gtk_text_buffer_get_text(word->buffer, beg, &(word->start),
            /*don't include hidden chars*/FALSE);
    g_autofree gchar *prefix = g_markup_escape_text(tmp, /*NULL terminated string*/-1);

    tmp = gtk_text_buffer_get_text(word->buffer, &(word->start), &(word->end),
            /*don't include hidden chars*/FALSE);
    g_autofree gchar *text = g_markup_escape_text(tmp, /*NULL terminated string*/-1);

    tmp = gtk_text_buffer_get_text(word->buffer, &(word->end), end,
            /*don't include hidden chars*/FALSE);
    g_autofree gchar *suffix = g_markup_escape_text(tmp, /*NULL terminated string*/-1);

    char *content = g_strdup_printf("%s<span foreground=\"red\">%s</span>%s", prefix, text, suffix);

    gtk_text_iter_free(end);
    gtk_text_iter_free(beg);

    gtk_list_store_set(store, new_row,
            SEARCH_COLUMN_FILE, file_name,
            SEARCH_COLUMN_TEXT, content,
            SEARCH_COLUMN_ITEM, word,
            -1);
}

static bool highlight_words_in_textview(int page, GtkTextView *tev, GList *words, GList *ignored_words, bool case_insensitive)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tev);
    gtk_text_buffer_set_modified(buffer, FALSE);

    GtkWidget *notebook_child = gtk_notebook_get_nth_page(g_notebook, page);
    GtkWidget *tab_lbl = gtk_notebook_get_tab_label(g_notebook, notebook_child);

    /* Remove old results */
    bool buffer_removing = false;

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_sensitive_list), &iter);

    /* Turn off the changed callback during the update */
    g_signal_handler_block(g_tv_sensitive_sel, g_tv_sensitive_sel_hndlr);

    while (valid)
    {
        g_autofree char *text = NULL;
        search_item_t *word = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(g_ls_sensitive_list), &iter,
                SEARCH_COLUMN_TEXT, &text,
                SEARCH_COLUMN_ITEM, &word,
                -1);

        if (word->buffer == buffer)
        {
            buffer_removing = true;

            valid = gtk_list_store_remove(g_ls_sensitive_list, &iter);

            if (word == g_current_highlighted_word)
                g_current_highlighted_word = NULL;

            free(word);
        }
        else
        {
            if(buffer_removing)
                break;

            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_sensitive_list), &iter);
        }
    }

    /* Turn on the changed callback after the update */
    g_signal_handler_unblock(g_tv_sensitive_sel, g_tv_sensitive_sel_hndlr);

    GtkTextIter start_find;
    gtk_text_buffer_get_start_iter(buffer, &start_find);
    GtkTextIter end_find;
    gtk_text_buffer_get_end_iter(buffer, &end_find);

    gtk_text_buffer_remove_tag_by_name(buffer, "search_result_bg", &start_find, &end_find);
    gtk_text_buffer_remove_tag_by_name(buffer, "current_result_bg", &start_find, &end_find);

    PangoAttrList *attrs = gtk_label_get_attributes(GTK_LABEL(tab_lbl));
    gtk_label_set_attributes(GTK_LABEL(tab_lbl), NULL);
    pango_attr_list_unref(attrs);

    GList *result = NULL;
    GList *ignored_words_in_buffer = NULL;

    ignored_words_in_buffer = find_words_in_text_buffer(page,
                                                        tev,
                                                        ignored_words,
                                                        NULL,
                                                        start_find,
                                                        end_find,
                                                        /*case sensitive*/false);


    result = find_words_in_text_buffer(page,
                                       tev,
                                       words,
                                       ignored_words_in_buffer,
                                       start_find,
                                       end_find,
                                       case_insensitive
                                        );

    for (GList *w = result; w; w = g_list_next(w))
    {
        search_item_t *item = (search_item_t *)w->data;
        gtk_text_buffer_apply_tag_by_name(buffer, "search_result_bg",
                                          sitem_get_start_iter(item),
                                          sitem_get_end_iter(item));
    }

    if (result)
    {
        PangoAttrList *attrs = pango_attr_list_new();
        PangoAttribute *foreground_attr = pango_attr_foreground_new(65535, 0, 0);
        pango_attr_list_insert(attrs, foreground_attr);
        PangoAttribute *underline_attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        pango_attr_list_insert(attrs, underline_attr);
        gtk_label_set_attributes(GTK_LABEL(tab_lbl), attrs);

        /* The current order of the found words is defined by order of words in the
         * passed list. We have to order the words according to their occurrence in
         * the buffer.
         */
        result = g_list_sort(result, (GCompareFunc)sitem_compare);

        GList *search_result = result;
        for ( ; search_result != NULL; search_result = g_list_next(search_result))
        {
            search_item_t *word = (search_item_t *)search_result->data;

            const gchar *file_name = gtk_label_get_text(GTK_LABEL(tab_lbl));

            /* Create a new row */
            GtkTreeIter new_row;
            if (valid)
                /* iter variable is valid GtkTreeIter and it means that the results */
                /* need to be inserted before this iterator, in this case iter points */
                /* to the first word of another GtkTextView */
                gtk_list_store_insert_before(g_ls_sensitive_list, &new_row, &iter);
            else
                /* the GtkTextView is the last one or the only one, insert the results */
                /* at the end of the list store */
                gtk_list_store_append(g_ls_sensitive_list, &new_row);

            /* Assign values to the new row */
            search_item_to_list_store_item(g_ls_sensitive_list, &new_row, file_name, word);
        }
    }

    g_list_free_full(ignored_words_in_buffer, free);
    g_list_free(result);

    return result != NULL;
}

static gboolean highligh_words_in_tabs(GList *forbidden_words,  GList *allowed_words, bool case_insensitive)
{
    gboolean found = false;

    gint n_pages = gtk_notebook_get_n_pages(g_notebook);
    int page = 0;
    for (page = 0; page < n_pages; page++)
    {
        //notebook_page->scrolled_window->text_view
        GtkWidget *notebook_child = gtk_notebook_get_nth_page(g_notebook, page);
        GtkWidget *tab_lbl = gtk_notebook_get_tab_label(g_notebook, notebook_child);

        const char *const lbl_txt = gtk_label_get_text(GTK_LABEL(tab_lbl));
        if (strncmp(lbl_txt, "page 1", 5) == 0 || strcmp(FILENAME_COMMENT, lbl_txt) == 0)
            continue;

        GtkTextView *tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        found |= highlight_words_in_textview(page, tev, forbidden_words, allowed_words, case_insensitive);
    }

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_sensitive_list), &iter))
        gtk_tree_selection_select_iter(g_tv_sensitive_sel, &iter);

    return found;
}

static gboolean highlight_forbidden(void)
{
    GList *forbidden_words = libreport_load_words_from_file(FORBIDDEN_WORDS_BLACKLLIST);
    GList *allowed_words = libreport_load_words_from_file(FORBIDDEN_WORDS_WHITELIST);

    const gboolean result = highligh_words_in_tabs(forbidden_words, allowed_words, /*case sensitive*/false);

    g_list_free_full(allowed_words, free);
    g_list_free_full(forbidden_words, free);

    return result;
}

static char *get_next_processed_event(GList **events_list)
{
    if (!events_list || !*events_list)
        return NULL;

    char *event_name = (char *)(*events_list)->data;
    *events_list = g_list_delete_link(*events_list, *events_list);

    clear_warnings();

    char *warning = NULL;
    const bool acceptable = check_minimal_bt_rating(event_name, &warning);

    if (warning != NULL)
    {
        add_warning(warning, GTK_CONTAINER(g_report_warning_label_box));
        g_clear_pointer(&warning, g_free);

        gtk_widget_show(g_report_warning_label_box);
    }

    if (!acceptable)
    {
        /* a node for this event was already removed */
        free(event_name);

        g_list_free_full(*events_list, free);
        *events_list = NULL;
        return NULL;
    }

    return event_name;
}

static void update_private_ticket_creation_warning_for_selected_event(void)
{
    event_config_t *cfg = get_event_config(g_event_selected);
    if (cfg == NULL || !cfg->ec_supports_restricted_access)
        return;

    int flags = PRIV_WARN_SHOW_BTN | PRIV_WARN_HIDE_MSG;
    if (ec_restricted_access_enabled(cfg))
        flags |= PRIV_WARN_BTN_CHECKED;

    private_ticket_creation_warning(flags);
}

static void on_page_prepare(GtkNotebook *assistant, GtkWidget *page, gpointer user_data)
{
    /* If processing is finished and if it was terminated because of an error
     * the event progress page is selected. So, it does not make sense to show
     * the next step button and we MUST NOT clear warnings.
     */
    if (!is_processing_finished())
    {
        /* Some pages hide it, so restore it to its default. */
        show_next_step_button();
        clear_warnings();
    }

    gtk_widget_hide(g_btn_detail);
    gtk_widget_hide(g_btn_repeat);
    /* Save text fields if changed */
    /* Must be called before any GUI operation because the following two
     * functions causes recreating of the text items tabs, thus all updates to
     * these tabs will be lost */
    save_items_from_notepad();
    save_text_from_text_view(g_tv_comment, FILENAME_COMMENT);

    bool complex_details = g_event_selected
                           && event_requires_details(g_event_selected);

    if (complex_details)
    {
        save_text_from_text_view(g_tv_steps, FILENAME_REPRODUCER);

        int reproducible = gtk_combo_box_get_active(GTK_COMBO_BOX(g_cmb_reproducible));
        if (reproducible > -1)
        {
            const char *reproducible_str = get_problem_data_reproducible_name(reproducible);
            if (reproducible_str != NULL)
            {
                struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
                if (dd)
                    dd_save_text(dd, FILENAME_REPRODUCIBLE, reproducible_str);
                else
                    error_msg(_("Failed to save file '%s'"), FILENAME_REPRODUCIBLE);

                dd_close(dd);
            }
        }
    }
    problem_data_reload_from_dump_dir();
    update_gui_state_from_problem_data(/* don't update selected event */ 0);

    if (pages[PAGENO_SUMMARY].page_widget == page)
    {
        if (libreport_get_global_create_private_ticket())
            private_ticket_creation_warning(  PRIV_WARN_SHOW_BTN
                                            | PRIV_WARN_BTN_CHECKED
                                            | PRIV_WARN_HIDE_MSG);

        /* Skip intro screen */
        int n = select_next_page_no(PAGENO_SUMMARY);
        log_info("Switching from intro to page no. %d", n);
        gtk_notebook_set_current_page(assistant, n);

        return;
    }

    if (pages[PAGENO_EDIT_ELEMENTS].page_widget == page)
    {
        if (highlight_forbidden())
        {
            add_sensitive_data_warning();
            show_warnings();
            gtk_expander_set_expanded(g_exp_search, TRUE);
        }
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_rb_custom_search), TRUE);

        show_warnings();
    }

    if (pages[PAGENO_REVIEW_DATA].page_widget == page)
    {
        update_ls_details_checkboxes(g_event_selected);
        gtk_widget_set_sensitive(g_btn_next, gtk_toggle_button_get_active(g_tb_approve_bt));
    }

    if (pages[PAGENO_EDIT_COMMENT].page_widget == page)
    {

        bool complex_details = g_event_selected
                               && event_requires_details(g_event_selected);

        gtk_widget_set_visible(GTK_WIDGET(g_vb_simple_details), !complex_details);
        gtk_widget_set_visible(GTK_WIDGET(g_vb_complex_details), complex_details);
        update_private_ticket_creation_warning_for_selected_event();

        gtk_widget_show(g_btn_detail);
        gtk_widget_set_sensitive(g_btn_next, false);
        on_comment_changed(gtk_text_view_get_buffer(g_tv_comment), NULL);
    }

    if (pages[PAGENO_EVENT_PROGRESS].page_widget == page)
    {
        while (g_event_selected && g_event_selected[0])
        {
            log_notice("Selected event '%s'; going to run its actions now...",
                       g_event_selected);
            clear_warnings();
            start_event_run(g_event_selected);

            /* Automatically proceed with the next event or page if
             * - no command was spawned by start_event_run,
             * - there are events still left to be run, and
             * - we're in cruise control mode (i.e. not verbose).
             */
            if (g_event_child_pid < 0 && !is_processing_finished() && !libreport_g_verbose)
            {
                gint next_page_no = select_next_page_no(PAGENO_EVENT_PROGRESS);

                /* If the page doesn't change, the switch-page signal is not
                 * emitted and on_page_prepare() is not called. Hence we jump back
                 * and continue as if this function were called in the updated
                 * environment.
                 */
                if (next_page_no == PAGENO_EVENT_PROGRESS)
                    continue;

                gtk_notebook_set_current_page(g_assistant, next_page_no);
            }

            break;
        }
    }

    if (pages[PAGENO_EVENT_SELECTOR].page_widget == page)
    {
        if (is_processing_finished())
            hide_next_step_button();
    }
}

static void set_auto_event_chain(GtkButton *button, gpointer user_data)
{
    workflow_t *w = (workflow_t *)user_data;
    config_item_info_t *info = workflow_get_config_info(w);
    log_notice("selected workflow '%s'", ci_get_screen_name(info));

    GList *wf_event_list = wf_get_event_list(w);
    while(wf_event_list)
    {
        g_auto_event_list = g_list_append(g_auto_event_list, g_strdup(ec_get_name(wf_event_list->data)));
        libreport_load_single_event_config_data_from_user_storage((event_config_t *)wf_event_list->data);

        wf_event_list = g_list_next(wf_event_list);
    }

    gint current_page_no = gtk_notebook_get_current_page(g_assistant);
    gint next_page_no = select_next_page_no(current_page_no);

    /* If page is not changed, 'switch-page' signal is not emitted. */
    if (current_page_no == next_page_no)
        on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, next_page_no), NULL);
    else
        gtk_notebook_set_current_page(g_assistant, next_page_no);

    /* Show Next Step button which was hidden on Selector page in non-expert
     * mode. Next Step button must be hidden because Selector page shows only
     * workflow buttons in non-expert mode.
     */
    show_next_step_button();
}

static void add_workflow_buttons(GtkBox *box, GHashTable *workflows, GCallback func)
{
    gtk_container_foreach(GTK_CONTAINER(box), &remove_child_widget, NULL);

    GList *possible_workflows = list_possible_events_glist(g_dump_dir_name, "workflow");
    GHashTable *workflow_table = load_workflow_config_data_from_list(
                        possible_workflows,
                        WORKFLOWS_DIR);
    g_list_free_full(possible_workflows, free);
    g_object_set_data_full(G_OBJECT(box), "workflows", workflow_table, (GDestroyNotify)g_hash_table_destroy);

    GList *wf_list = g_hash_table_get_values(workflow_table);
    wf_list = g_list_sort(wf_list, (GCompareFunc)wf_priority_compare);

    for (GList *wf_iter = wf_list; wf_iter; wf_iter = g_list_next(wf_iter))
    {
        workflow_t *w = (workflow_t *)wf_iter->data;
        g_autofree char *btn_label = g_strdup_printf("<b>%s</b>\n%s", wf_get_screen_name(w),
                wf_get_description(w));
        GtkWidget *button = gtk_button_new_with_label(btn_label);
        GList *children = gtk_container_get_children(GTK_CONTAINER(button));
        GtkWidget *label = (GtkWidget *)children->data;
        gtk_label_set_use_markup(GTK_LABEL(label), true);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_top(label, 10);

        gtk_widget_set_margin_start(label, 40);
		
        gtk_widget_set_margin_bottom(label, 10);
        g_list_free(children);
        g_signal_connect(button, "clicked", func, w);
        gtk_box_pack_start(box, button, true, false, 2);
    }

    g_list_free(wf_list);
}

static char *setup_next_processed_event(GList **events_list)
{
    g_clear_pointer(&g_event_selected, free);

    char *event = get_next_processed_event(events_list);
    if (!event)
    {
        /* No next event, go to progress page and finish. */
        gtk_label_set_text(g_lbl_event_log, _("Processing finished."));
        /* We don't know the result of the previous event here
         * so at least hide the spinner, because we're obviously finished.
         */
        gtk_widget_hide(GTK_WIDGET(g_spinner_event_log));
        hide_next_step_button();
        return NULL;
    }

    log_notice("selected -e EVENT:%s", event);
    return event;
}

static bool get_sensitive_data_permission(const char *event_name)
{
    event_config_t *event_cfg = get_event_config(event_name);

    if (!event_cfg || !event_cfg->ec_sending_sensitive_data)
        return true;

    const char *screen_name = ec_get_screen_name(event_cfg);
    if (screen_name)
        event_name = screen_name;

    g_autofree char *msg = g_strdup_printf(
            _("Event '%s' requires permission to send possibly sensitive data.\n"
              "Do you want to continue?"),
            event_name);
    const bool response = libreport_run_ask_yes_no_yesforever_dialog("ask_send_sensitive_data",
            msg, GTK_WINDOW(g_wnd_assistant));

    return response;
}

static gint select_next_page_no(gint current_page_no)
{
    GtkWidget *page;

 again:
    log_notice("%s: current_page_no:%d", __func__, current_page_no);
    current_page_no++;
    page = gtk_notebook_get_nth_page(g_assistant, current_page_no);

    if (pages[PAGENO_EVENT_SELECTOR].page_widget == page)
    {
        if (is_processing_finished())
        {
            return current_page_no; //stay here and let user select the workflow
        }

        log_info("%s: Looking for next event to process", __func__);
        /* (note: this frees and sets to NULL g_event_selected) */
        char *event = setup_next_processed_event(&g_auto_event_list);
        if (!event)
        {
            current_page_no = PAGENO_EVENT_PROGRESS - 1;
            goto again;
        }

        if (!get_sensitive_data_permission(event))
        {
            free(event);

            cancel_processing(g_lbl_event_log, /* default message */ NULL, TERMINATE_NOFLAGS);
            current_page_no = PAGENO_EVENT_PROGRESS - 1;
            goto again;
        }

        if (problem_data_get_content_or_NULL(g_cd, FILENAME_NOT_REPORTABLE))
        {

            g_autofree char *msg = g_strdup_printf(_("This problem should not be reported "
                            "(it is likely a known problem). %s"),
                            problem_data_get_content_or_NULL(g_cd, FILENAME_NOT_REPORTABLE)
            );

            if (libreport_get_global_stop_on_not_reportable())
            {
                free(event);

                cancel_processing(g_lbl_event_log, msg, TERMINATE_NOFLAGS);
                current_page_no = PAGENO_EVENT_PROGRESS - 1;
                goto again;
            }
            else
            {
                log_warning("%s", msg);
            }
        }

        g_event_selected = event;

        /* Notify the user that some configuration options miss values, but don't
         * force him to provide them.
         */
        check_event_config(g_event_selected);

        current_page_no = PAGENO_EVENT_SELECTOR + 1;
        goto event_was_selected;
    }

    if (pages[PAGENO_EVENT_SELECTOR + 1].page_widget == page)
    {
 event_was_selected:
        if (!g_event_selected)
        {
            /* Go back to selectors */
            current_page_no = PAGENO_EVENT_SELECTOR - 1;
            goto again;
        }

        if (!event_need_review(g_event_selected))
        {
            current_page_no = PAGENO_EVENT_PROGRESS - 1;
            goto again;
        }
    }

    if (pages[PAGENO_EVENT_DONE].page_widget == page)
    {
        if (g_auto_event_list)
        {
            /* Go back to selectors */
            current_page_no = PAGENO_SUMMARY;
        }
        goto again;
    }

    if (pages[PAGENO_NOT_SHOWN].page_widget == page)
    {
        exit(0);
    }

    log_notice("%s: selected page #%d", __func__, current_page_no);
    return current_page_no;
}

static void rehighlight_forbidden_words(int page, GtkTextView *tev)
{
    GList *forbidden_words = libreport_load_words_from_file(FORBIDDEN_WORDS_BLACKLLIST);
    GList *allowed_words = libreport_load_words_from_file(FORBIDDEN_WORDS_WHITELIST);

    highlight_words_in_textview(page, tev, forbidden_words, allowed_words, /*case sensitive*/false);

    g_list_free_full(allowed_words, free);
    g_list_free_full(forbidden_words, free);
}

static void on_sensitive_word_selection_changed(GtkTreeSelection *sel, gpointer user_data)
{
    search_item_t *old_word = g_current_highlighted_word;
    g_current_highlighted_word = NULL;

    if (old_word && FALSE == gtk_text_buffer_get_modified(old_word->buffer))
        gtk_text_buffer_remove_tag_by_name(old_word->buffer, "current_result_bg", &(old_word->start), &(old_word->end));

    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    search_item_t *new_word;
    gtk_tree_model_get(model, &iter,
            SEARCH_COLUMN_ITEM, &new_word,
            -1);

    if (gtk_text_buffer_get_modified(new_word->buffer))
    {
        if (g_search_text == NULL)
            rehighlight_forbidden_words(new_word->page, new_word->tev);
        else
        {
            log_notice("searching again: '%s'", g_search_text);
            GList *searched_words = g_list_append(NULL, (gpointer)g_search_text);
            highlight_words_in_textview(new_word->page, new_word->tev, searched_words, NULL, /*case insensitive*/true);
            g_list_free(searched_words);
        }

        return;
    }

    g_current_highlighted_word = new_word;

    gtk_notebook_set_current_page(g_notebook, new_word->page);
    gtk_text_buffer_apply_tag_by_name(new_word->buffer, "current_result_bg", &(new_word->start), &(new_word->end));
    gtk_text_buffer_place_cursor(new_word->buffer, &(new_word->start));
    gtk_text_view_scroll_to_iter(new_word->tev, &(new_word->start), 0.0, false, 0, 0);
}

static void highlight_search(GtkEntry *entry)
{
    g_search_text = gtk_entry_get_text(entry);

    log_notice("searching: '%s'", g_search_text);
    GList *words = g_list_append(NULL, (gpointer)g_search_text);
    highligh_words_in_tabs(words, NULL, /*case insensitive*/true);
    g_list_free(words);
}

static gboolean highlight_search_on_timeout(gpointer user_data)
{
    g_timeout = 0;
    highlight_search(GTK_ENTRY(user_data));
    /* returning false will make glib to remove this event */
    return false;
}

static void search_timeout(GtkEntry *entry)
{
    /* this little hack makes the search start searching after 500 milisec after
     * user stops writing into entry box
     * if this part is removed, then the search will be started on every
     * change of the search entry
     */
    if (g_timeout != 0)
        g_source_remove(g_timeout);
    g_timeout = g_timeout_add(500, &highlight_search_on_timeout, (gpointer)entry);
}

static void on_forbidden_words_toggled(GtkToggleButton *btn, gpointer user_data)
{
    g_search_text = NULL;
    log_notice("nothing to search for, highlighting forbidden words instead");
    highlight_forbidden();
}

static void on_custom_search_toggled(GtkToggleButton *btn, gpointer user_data)
{
    const gboolean custom_search = gtk_toggle_button_get_active(btn);
    gtk_widget_set_sensitive(GTK_WIDGET(g_search_entry_bt), custom_search);

    if (custom_search)
        highlight_search(g_search_entry_bt);
}

static void save_edited_one_liner(GtkCellRendererText *renderer,
                gchar *tree_path,
                gchar *new_text,
                gpointer user_data)
{
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(g_ls_details), &iter, tree_path))
        return;
    gchar *item_name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                DETAIL_COLUMN_NAME, &item_name,
                -1);
    if (!item_name) /* paranoia, should never happen */
        return;
    struct problem_item *item = problem_data_get_item_or_NULL(g_cd, item_name);
    if (item && (item->flags & CD_FLAG_ISEDITABLE))
    {
        struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
        if (dd)
        {
            dd_save_text(dd, item_name, new_text);
            free(item->content);
            item->content = g_strdup(new_text);
            gtk_list_store_set(g_ls_details, &iter,
                    DETAIL_COLUMN_VALUE, new_text,
                    -1);
        }
        dd_close(dd);
    }
}

static void on_btn_add_file(GtkButton *button)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Attach File",
            GTK_WINDOW(g_wnd_assistant),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Open"), GTK_RESPONSE_ACCEPT,
            NULL
    );
    g_autofree char *filename = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    g_autofree char *message = NULL;

    if (filename)
    {
        char *basename = strrchr(filename, '/');
        if (!basename)  /* wtf? (never happens) */
            return;
        basename++;

        /* TODO: ask for the name to save it as? For now, just use basename */

        struct stat statbuf;
        if (stat(filename, &statbuf) != 0 || !S_ISREG(statbuf.st_mode))
        {
            message = g_strdup_printf(_("'%s' is not an ordinary file"), filename);
            goto show_msgbox;
        }

        struct problem_item *item = problem_data_get_item_or_NULL(g_cd, basename);
        if (!item || (item->flags & CD_FLAG_ISEDITABLE))
        {
            struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
            if (dd)
            {
                dd_close(dd);
                g_autofree char *new_name = g_build_filename(g_dump_dir_name, basename, NULL);
                if (strcmp(filename, new_name) == 0)
                    message = g_strdup(_("You are trying to copy a file onto itself"));
                else
                {
                    off_t r = libreport_copy_file(filename, new_name, 0666);
                    if (r < 0)
                    {
                        message = g_strdup_printf(_("Can't copy '%s': %s"), filename, strerror(errno));
                        unlink(new_name);
                    }
                    if (!message)
                    {
                        problem_data_reload_from_dump_dir();
                        update_gui_state_from_problem_data(/* don't update selected event */ 0);
                        /* Set flags for the new item */
                        update_ls_details_checkboxes(g_event_selected);
                    }
                }
            }
        }
        else
            message = g_strdup_printf(_("Item '%s' already exists and is not modifiable"), basename);

        if (message)
        {
 show_msgbox: ;
            GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING,
                GTK_BUTTONS_CLOSE,
                "%s", message);
            gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(g_wnd_assistant));
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
        }
    }
}

static void on_btn_detail(GtkButton *button)
{
    GtkWidget *pdd = problem_details_dialog_new(g_cd, g_wnd_assistant);
    gtk_dialog_run(GTK_DIALOG(pdd));
}

/* [Del] key handling in item list */
static void delete_item(GtkTreeView *treeview)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    if (selection)
    {
        GtkTreeIter iter;
        GtkTreeModel *store = gtk_tree_view_get_model(treeview);
        if (gtk_tree_selection_get_selected(selection, &store, &iter) == TRUE)
        {
            GValue d_item_name = { 0 };
            gtk_tree_model_get_value(store, &iter, DETAIL_COLUMN_NAME, &d_item_name);
            const char *item_name = g_value_get_string(&d_item_name);
            if (item_name)
            {
                struct problem_item *item = problem_data_get_item_or_NULL(g_cd, item_name);
                if (item->flags & CD_FLAG_ISEDITABLE)
                {
//                  GtkTreePath *old_path = gtk_tree_model_get_path(store, &iter);

                    struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
                    if (dd)
                    {
                        g_autofree char *filename = g_build_filename(g_dump_dir_name, item_name, NULL);
                        unlink(filename);
                        dd_close(dd);
                        g_hash_table_remove(g_cd, item_name);
                        gtk_list_store_remove(g_ls_details, &iter);
                    }

//                  /* Try to retain the same cursor position */
//                  sanitize_cursor(old_path);
//                  gtk_tree_path_free(old_path);
                }
            }
        }
    }
}
static gint on_key_press_event_in_item_list(GtkTreeView *treeview, GdkEventKey *key, gpointer unused)
{
    int k = key->keyval;

    if (k == GDK_KEY_Delete || k == GDK_KEY_KP_Delete)
    {
        delete_item(treeview);
        return TRUE;
    }
    return FALSE;
}

static void on_reproducible_changed(GtkComboBox *widget, gpointer user_data)
{
    update_reproducible_hints();
    toggle_eb_comment();
}

/* Initialization */

static void on_next_btn_cb(GtkWidget *btn, gpointer user_data)
{
    gint current_page_no = gtk_notebook_get_current_page(g_assistant);
    gint next_page_no = select_next_page_no(current_page_no);

    /* If page is not changed, 'switch-page' signal is not emitted. */
    if (current_page_no == next_page_no)
        on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, next_page_no), NULL);
    else
        gtk_notebook_set_current_page(g_assistant, next_page_no);
}

static void add_pages(void)
{
    g_builder = make_builder();

    if (g_provider == NULL)
        load_css_style();

    int i;
    for (i = 0; page_names[i] != NULL; i++)
    {
        GtkWidget *page = GTK_WIDGET(gtk_builder_get_object(g_builder, page_names[i]));
        pages[i].page_widget = page;
        gtk_notebook_append_page(g_assistant, page, gtk_label_new(pages[i].title));
        log_notice("added page: %s", page_names[i]);
    }

    /* Set pointers to objects we might need to work with */
    g_lbl_cd_reason            = GTK_LABEL(         gtk_builder_get_object(g_builder, "lbl_cd_reason"));
    g_box_events               = GTK_BOX(           gtk_builder_get_object(g_builder, "vb_events"));
    g_box_workflows            = GTK_BOX(           gtk_builder_get_object(g_builder, "vb_workflows"));
    g_lbl_event_log            = GTK_LABEL(         gtk_builder_get_object(g_builder, "lbl_event_log"));
    g_tv_event_log             = GTK_TEXT_VIEW(     gtk_builder_get_object(g_builder, "tv_event_log"));
    g_tv_comment               = GTK_TEXT_VIEW(     gtk_builder_get_object(g_builder, "tv_comment"));
    g_eb_comment               = GTK_EVENT_BOX(     gtk_builder_get_object(g_builder, "eb_comment"));
    g_cb_no_comment            = GTK_CHECK_BUTTON(  gtk_builder_get_object(g_builder, "cb_no_comment"));
    g_tv_details               = GTK_TREE_VIEW(     gtk_builder_get_object(g_builder, "tv_details"));
    g_tb_approve_bt            = GTK_TOGGLE_BUTTON( gtk_builder_get_object(g_builder, "cb_approve_bt"));
    g_search_entry_bt          = GTK_ENTRY(         gtk_builder_get_object(g_builder, "entry_search_bt"));
    g_container_details1       = GTK_CONTAINER(     gtk_builder_get_object(g_builder, "container_details1"));
    g_container_details2       = GTK_CONTAINER(     gtk_builder_get_object(g_builder, "container_details2"));
    g_btn_add_file             = GTK_BUTTON(        gtk_builder_get_object(g_builder, "btn_add_file"));
    g_lbl_size                 = GTK_LABEL(         gtk_builder_get_object(g_builder, "lbl_size"));
    g_notebook                 = GTK_NOTEBOOK(      gtk_builder_get_object(g_builder, "notebook_edit"));
    g_ls_sensitive_list        = GTK_LIST_STORE(    gtk_builder_get_object(g_builder, "ls_sensitive_words"));
    g_tv_sensitive_list        = GTK_TREE_VIEW(     gtk_builder_get_object(g_builder, "tv_sensitive_words"));
    g_tv_sensitive_sel         = GTK_TREE_SELECTION(gtk_builder_get_object(g_builder, "tv_sensitive_words_selection"));
    g_rb_forbidden_words       = GTK_RADIO_BUTTON(  gtk_builder_get_object(g_builder, "rb_forbidden_words"));
    g_rb_custom_search         = GTK_RADIO_BUTTON(  gtk_builder_get_object(g_builder, "rb_custom_search"));
    g_exp_search               = GTK_EXPANDER(      gtk_builder_get_object(g_builder, "expander_search"));
    g_report_stack             = GTK_WIDGET(        gtk_builder_get_object(g_builder, "report-stack"));
    g_report_status_box        = GTK_WIDGET(        gtk_builder_get_object(g_builder, "report-status-box"));
    g_report_warning_box       = GTK_WIDGET(        gtk_builder_get_object(g_builder, "report-warning-box"));
    g_spinner_event_log        = GTK_SPINNER(       gtk_builder_get_object(g_builder, "spinner_event_log"));
    g_img_process_fail         = GTK_IMAGE(         gtk_builder_get_object(g_builder, "img_process_fail"));
    g_report_warning_label_box = GTK_WIDGET(        gtk_builder_get_object(g_builder, "report-warning-label-box"));
    g_exp_report_log           = GTK_EXPANDER(      gtk_builder_get_object(g_builder, "expand_report"));
    g_vb_simple_details        = GTK_BOX(           gtk_builder_get_object(g_builder, "vb_simple_details"));
    g_cmb_reproducible         = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(g_builder, "cmb_reproducible"));
    g_tv_steps                 = GTK_TEXT_VIEW(     gtk_builder_get_object(g_builder, "tv_steps"));
    g_vb_complex_details       = GTK_BOX(           gtk_builder_get_object(g_builder, "vb_complex_details"));
    g_lbl_complex_details_hint = GTK_LABEL(         gtk_builder_get_object(g_builder, "lbl_complex_details_hint"));

    gtk_widget_set_no_show_all(GTK_WIDGET(g_spinner_event_log), true);

    gtk_widget_set_name(GTK_WIDGET(g_tv_event_log), "g_tv_event_log");
    gtk_widget_set_name(GTK_WIDGET(g_eb_comment), "g_eb_comment");

    fix_all_wrapped_labels(GTK_WIDGET(g_assistant));

    g_signal_connect(g_cb_no_comment, "toggled", G_CALLBACK(on_no_comment_toggled), NULL);

    g_signal_connect(g_rb_forbidden_words, "toggled", G_CALLBACK(on_forbidden_words_toggled), NULL);
    g_signal_connect(g_rb_custom_search, "toggled", G_CALLBACK(on_custom_search_toggled), NULL);


    g_signal_connect(g_tv_details, "key-press-event", G_CALLBACK(on_key_press_event_in_item_list), NULL);
    g_tv_sensitive_sel_hndlr = g_signal_connect(g_tv_sensitive_sel, "changed", G_CALLBACK(on_sensitive_word_selection_changed), NULL);

    gtk_combo_box_text_insert(g_cmb_reproducible, PROBLEM_REPRODUCIBLE_UNKNOWN, NULL,
                            _("I have experienced this problem for the first time"));

    gtk_combo_box_text_insert(g_cmb_reproducible, PROBLEM_REPRODUCIBLE_YES, NULL,
                            _("I can reproduce this problem"));

    gtk_combo_box_text_insert(g_cmb_reproducible, PROBLEM_REPRODUCIBLE_RECURRENT, NULL,
                            _("This problem occurs repeatedly"));

    g_signal_connect(g_cmb_reproducible, "changed", G_CALLBACK(on_reproducible_changed), NULL);

}

static void create_details_treeview(void)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Include"), renderer,
                /* which "attr" of renderer to set from which COLUMN? (can be repeated) */
                "active", DETAIL_COLUMN_CHECKBOX,
                NULL);
    g_tv_details_col_checkbox = column;
    gtk_tree_view_append_column(g_tv_details, column);
    /* This column has a handler */
    g_signal_connect(renderer, "toggled", G_CALLBACK(g_tv_details_checkbox_toggled), NULL);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Name"), renderer,
                "text", DETAIL_COLUMN_NAME,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);
    /* This column has a clickable header for sorting */
    gtk_tree_view_column_set_sort_column_id(column, DETAIL_COLUMN_NAME);

    g_tv_details_renderer_value = renderer = gtk_cell_renderer_text_new();
    g_signal_connect(renderer, "edited", G_CALLBACK(save_edited_one_liner), NULL);
    column = gtk_tree_view_column_new_with_attributes(
                _("Value"), renderer,
                "text", DETAIL_COLUMN_VALUE,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);
    /* This column has a clickable header for sorting */
    gtk_tree_view_column_set_sort_column_id(column, DETAIL_COLUMN_VALUE);

    /*
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Path"), renderer,
                "text", DETAIL_COLUMN_PATH,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);
    */

    g_ls_details = gtk_list_store_new(DETAIL_NUM_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(g_tv_details, GTK_TREE_MODEL(g_ls_details));

    g_signal_connect(g_tv_details, "row-activated", G_CALLBACK(tv_details_row_activated), NULL);
    g_signal_connect(g_tv_details, "cursor-changed", G_CALLBACK(tv_details_cursor_changed), NULL);
    g_signal_connect(g_tv_details, "map", G_CALLBACK(tv_details_show), NULL);
    /* [Enter] on a row:
     * g_signal_connect(g_tv_details, "select-cursor-row", G_CALLBACK(tv_details_select_cursor_row), NULL);
     */
}

static void init_page(page_obj_t *page, const char *name, const char *title)
{
    page->name = name;
    page->title = title;
}

static void init_pages(void)
{
    init_page(&pages[0], PAGE_SUMMARY            , _("Problem description")   );
    init_page(&pages[1], PAGE_EVENT_SELECTOR     , _("Select how to report this problem"));
    init_page(&pages[2], PAGE_EDIT_COMMENT       , _("Provide additional information"));
    init_page(&pages[3], PAGE_EDIT_ELEMENTS      , _("Review the data")       );
    init_page(&pages[4], PAGE_REVIEW_DATA        , _("Confirm data to report"));
    init_page(&pages[5], PAGE_EVENT_PROGRESS     , _("Processing")            );
    init_page(&pages[6], PAGE_EVENT_DONE         , _("Processing done")       );
//do we still need this?
    init_page(&pages[7], PAGE_NOT_SHOWN          , ""                         );
}

static void assistant_quit_cb(void *obj, void *data)
{
    /* Suppress execution of consume_cmd_output() */
    if (g_event_source_id != 0)
    {
        g_source_remove(g_event_source_id);
        g_event_source_id = 0;
    }

    cancel_event_run();

    if (g_loaded_texts)
    {
        g_hash_table_destroy(g_loaded_texts);
        g_loaded_texts = NULL;
    }

    gtk_widget_destroy(GTK_WIDGET(g_wnd_assistant));
    g_wnd_assistant = (void *)0xdeadbeaf;
}

void create_assistant(GtkApplication *app)
{
    g_loaded_texts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    g_assistant = GTK_NOTEBOOK(gtk_notebook_new());

    /* Since someone thought it would be a bright idea to use a GtkNotebook
     * as GtkAssistant, we need to help the user not shoot themselves in the
     * face^Wfoot. Navigating freely will likely reset some state or flat out crash.
     */
    gtk_notebook_set_show_tabs(g_assistant, FALSE);

    g_btn_close = gtk_button_new_with_mnemonic(_("_Close"));
    gtk_button_set_image(GTK_BUTTON(g_btn_close), gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON));
    g_btn_stop = gtk_button_new_with_mnemonic(_("_Stop"));
    gtk_button_set_image(GTK_BUTTON(g_btn_stop), gtk_image_new_from_icon_name("process-stop-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_no_show_all(g_btn_stop, true); /* else gtk_widget_hide won't work */
    g_btn_repeat = gtk_button_new_with_label(_("Repeat"));
    gtk_widget_set_no_show_all(g_btn_repeat, true); /* else gtk_widget_hide won't work */
    g_btn_next = gtk_button_new_with_mnemonic(_("_Forward"));
    gtk_button_set_image(GTK_BUTTON(g_btn_next), gtk_image_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_no_show_all(g_btn_next, true); /* else gtk_widget_hide won't work */
    g_btn_detail = gtk_button_new_with_mnemonic(_("Details"));
    gtk_widget_set_no_show_all(g_btn_detail, true); /* else gtk_widget_hide won't work */

    g_box_buttons = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_box_pack_start(g_box_buttons, g_btn_close, false, false, 5);
    gtk_box_pack_start(g_box_buttons, g_btn_stop, false, false, 5);
    gtk_box_pack_start(g_box_buttons, g_btn_repeat, false, false, 5);
    /* Btns above are to the left, the rest are to the right: */

    gtk_widget_set_valign(GTK_WIDGET(g_btn_next), GTK_ALIGN_END);
    gtk_box_pack_end(g_box_buttons, g_btn_next, false, false, 5);
    gtk_box_pack_end(g_box_buttons, g_btn_detail, false, false, 5);

    {   /* Warnings area widget definition start */
        g_box_warning_labels = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(g_box_warning_labels), TRUE);

        GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(vbox), TRUE);
        gtk_box_pack_start(vbox, GTK_WIDGET(g_box_warning_labels), false, false, 5);

        GtkWidget *image = gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_visible(image, TRUE);

        g_widget_warnings_area = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_set_visible(g_widget_warnings_area, FALSE);
        gtk_widget_set_no_show_all(g_widget_warnings_area, TRUE);

        gtk_widget_set_valign(GTK_WIDGET(image), GTK_ALIGN_CENTER);
        gtk_widget_set_valign(GTK_WIDGET(vbox), GTK_ALIGN_CENTER);

        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), image, false, false, 5);
        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), GTK_WIDGET(vbox), false, false, 0);

    }   /* Warnings area widget definition end */

    g_box_assistant = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_assistant), true, true, 0);

    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_widget_warnings_area), false, false, 0);

    /* Private ticket warning */
    {
        g_sens_ticket = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_set_no_show_all(GTK_WIDGET(g_sens_ticket), TRUE);
        gtk_widget_hide(GTK_WIDGET(g_sens_ticket));

        g_sens_ticket_cb = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(_("Restrict access to the report")));
        gtk_widget_set_margin_start(GTK_WIDGET(g_sens_ticket_cb), 5);
        gtk_widget_show(GTK_WIDGET(g_sens_ticket_cb));
        g_signal_connect(g_sens_ticket_cb, "toggled", G_CALLBACK(on_sensitive_ticket_clicked_cb), NULL);

        GtkLinkButton *privacy_info_btn = GTK_LINK_BUTTON(gtk_link_button_new_with_label("", _("Learn more about restricted access in the configuration")));
        gtk_widget_show(GTK_WIDGET(privacy_info_btn));
        g_signal_connect(privacy_info_btn, "clicked", G_CALLBACK(on_privacy_info_btn), NULL);

        gtk_box_pack_start(GTK_BOX(g_sens_ticket), GTK_WIDGET(g_sens_ticket_cb), false, false, 5);
        gtk_box_pack_start(GTK_BOX(g_sens_ticket), GTK_WIDGET(privacy_info_btn), false, false, 5);

        gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_sens_ticket), false, true, 5);
    }

    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_box_buttons), false, false, 5);

    gtk_widget_show_all(GTK_WIDGET(g_box_buttons));
    gtk_widget_hide(g_btn_stop);
    gtk_widget_hide(g_btn_repeat);
    gtk_widget_show(g_btn_next);

    g_wnd_assistant = GTK_WINDOW(gtk_application_window_new(app));
    gtk_container_add(GTK_CONTAINER(g_wnd_assistant), GTK_WIDGET(g_box_assistant));

    gtk_window_set_default_size(g_wnd_assistant, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    /* set_default sets icon for every windows used in this app, so we don't
     * have to set the icon for those windows manually
     */
    gtk_window_set_default_icon_name("abrt");

    init_pages();

    add_pages();

    create_details_treeview();

    ProblemDetailsWidget *details = problem_details_widget_new(g_cd);
    gtk_container_add(GTK_CONTAINER(g_container_details1), GTK_WIDGET(details));

    g_signal_connect(g_btn_close, "clicked", G_CALLBACK(assistant_quit_cb), NULL);
    g_signal_connect(g_btn_stop, "clicked", G_CALLBACK(on_btn_cancel_event), NULL);
    g_signal_connect(g_btn_repeat, "clicked", G_CALLBACK(on_btn_repeat_cb), NULL);
    g_signal_connect(g_btn_next, "clicked", G_CALLBACK(on_next_btn_cb), NULL);

    g_signal_connect(g_wnd_assistant, "destroy", G_CALLBACK(assistant_quit_cb), NULL);
    g_signal_connect(g_assistant, "switch-page", G_CALLBACK(on_page_prepare), NULL);

    g_signal_connect(g_tb_approve_bt, "toggled", G_CALLBACK(on_bt_approve_toggle), NULL);
    g_signal_connect(gtk_text_view_get_buffer(g_tv_comment), "changed", G_CALLBACK(on_comment_changed), NULL);
    g_signal_connect(gtk_text_view_get_buffer(g_tv_steps),   "changed", G_CALLBACK(on_steps_changed),   NULL);

    g_signal_connect(g_btn_add_file, "clicked", G_CALLBACK(on_btn_add_file), NULL);
    g_signal_connect(g_btn_detail, "clicked", G_CALLBACK(on_btn_detail), NULL);

    g_signal_connect(g_search_entry_bt, "changed", G_CALLBACK(search_timeout), NULL);

    g_signal_connect(g_tv_event_log, "key-press-event", G_CALLBACK(key_press_event), NULL);
    g_signal_connect(g_tv_event_log, "event-after", G_CALLBACK(event_after), NULL);
    g_signal_connect(g_tv_event_log, "motion-notify-event", G_CALLBACK(motion_notify_event), NULL);
    g_signal_connect(g_tv_event_log, "visibility-notify-event", G_CALLBACK(visibility_notify_event), NULL);
    g_signal_connect(gtk_text_view_get_buffer(g_tv_event_log), "changed", G_CALLBACK(on_log_changed), NULL);

    hand_cursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_HAND2);
    regular_cursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_XTERM);

    /* Switch right to starting page. */
    on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, 0), NULL);
}
