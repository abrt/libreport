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

#define DEFAULT_WIDTH   800
#define DEFAULT_HEIGHT  500

#if GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 22
# define gtk_assistant_commit(...) ((void)0)
# define GDK_KEY_Delete GDK_Delete
# define GDK_KEY_KP_Delete GDK_KP_Delete
#endif

/* For Fedora 16 and gtk3 < 3.4.4*/
#ifndef GDK_BUTTON_PRIMARY
# define GDK_BUTTON_PRIMARY 1
#endif

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
static unsigned g_black_event_count = 0;

static pid_t g_event_child_pid = 0;

static bool g_expert_mode;

static GtkNotebook *g_assistant;
static GtkWindow *g_wnd_assistant;
static GtkBox *g_box_assistant;

static GtkWidget *g_btn_stop;
static GtkWidget *g_btn_close;
static GtkWidget *g_btn_next;

static GtkBox *g_box_events;
/* List of event_gui_data's */
static GList *g_list_events;
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
static gboolean g_warning_issued;

static GtkEventBox *g_ev_search_up;
static GtkEventBox *g_ev_search_down;
static GtkSpinner *g_spinner_event_log;

static GtkWidget *g_top_most_window;

typedef struct
{
    int page; //which tab in notepad
    GtkTextBuffer *buffer;
    GtkTextView *tev;
    GtkTextIter start;
    GtkTextIter end;
} search_item_t;

static GList *g_search_result_list;
static guint g_current_highlighted_word;
static bool g_first_highlight = true;

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

enum
{
    CLEAR_PREVIOUS_RESULT = 1 << 0,
};

/* Search in bt */
static guint g_timeout = 0;
static GtkEntry *g_search_entry_bt;

static GtkBuilder *g_builder;
static PangoFontDescription *g_monospace_font;

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
static const gchar PAGE_EVENT_SELECTOR[] = "page_2";
static const gchar PAGE_EDIT_COMMENT[]   = "page_1";
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

typedef struct
{
    const gchar *name;
    const gchar *title;
    GtkWidget *page_widget;
    int page_no;
} page_obj_t;

static page_obj_t pages[NUM_PAGES];

static struct strbuf *cmd_output = NULL;

/* Utility functions */

static void clear_warnings(void);
static void show_warnings(void);
static void add_warning(const char *warning);
static bool check_minimal_bt_rating(const char *event_name);
static const char *get_next_processed_event(GList **events_list);
static const char *setup_next_processed_event(GList **events_list);
static void setup_and_start_even_run(const char *event_name);
static void on_next_btn_cb(GtkWidget *btn, gpointer user_data);

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
        //const char *txt = gtk_label_get_label(label);
        GtkMisc *misc = (GtkMisc*)widget;
        gfloat yalign; //= 1;
        gint ypad; //= 1;
        if (gtk_label_get_line_wrap(label)
         && (gtk_misc_get_alignment(misc, NULL, &yalign), yalign == 0)
         && (gtk_misc_get_padding(misc, NULL, &ypad), ypad == 0)
        ) {
            //log("label '%s' set to wrap", txt);
            make_label_autowrap_on_resize(label);
            return;
        }
        //log("label '%s' not set to wrap %g %d", txt, yalign, ypad);
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

static void save_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    *(gint*)user_data = response_id;
}

static void on_configure_event_cb(GtkWidget *button, gpointer user_data)
{
    char *event_name = (char *)user_data;
    if (event_name != NULL)
    {
        int result = show_event_config_dialog(event_name, GTK_WINDOW(g_top_most_window));
        if (result == GTK_RESPONSE_APPLY)
        {
            GHashTable *errors = validate_event(event_name);
            if (errors == NULL)
            {
                gtk_widget_destroy(g_top_most_window);
                g_top_most_window = NULL;
            }
        }
    }
}

static void show_event_opt_error_dialog(const char *event_name)
{
    event_config_t *ec = get_event_config(event_name);
    char *message = xasprintf(_("Wrong settings detected for %s, "
                              "reporting will probably fail if you continue "
                              "with the current configuration."),
                               ec->screen_name);
    char *markup_message = xasprintf(_("Wrong settings detected for <b>%s</b>, "
                              "reporting will probably fail if you continue "
                              "with the current configuration."),
                               ec->screen_name);
    GtkWidget *wrong_settings = g_top_most_window = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_CLOSE,
        message);

    gtk_window_set_transient_for(GTK_WINDOW(wrong_settings), GTK_WINDOW(g_wnd_assistant));
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(wrong_settings),
                                    markup_message);
    free(message);
    free(markup_message);

    GtkWidget *act_area = gtk_dialog_get_content_area(GTK_DIALOG(wrong_settings));
    char * conf_btn_lbl = xasprintf(_("Con_figure %s"), ec->screen_name);
    GtkWidget *configure_event_btn = gtk_button_new_with_mnemonic(conf_btn_lbl);
    g_signal_connect(configure_event_btn, "clicked", G_CALLBACK(on_configure_event_cb), (gpointer)event_name);
    free(conf_btn_lbl);

    gtk_box_pack_start(GTK_BOX(act_area), configure_event_btn, false, false, 0);
    gtk_widget_show(configure_event_btn);


    gtk_dialog_run(GTK_DIALOG(wrong_settings));
    if (g_top_most_window)
        gtk_widget_destroy(wrong_settings);
}

static void update_window_title(void)
{
    /* prgname can be null according to gtk documentation */
    const char *prgname = g_get_prgname();
    const char *reason = problem_data_get_content_or_NULL(g_cd, FILENAME_REASON);
    char *title = xasprintf("%s - %s", (reason ? reason : g_dump_dir_name),
            (prgname ? prgname : "report"));
    gtk_window_set_title(g_wnd_assistant, title);
    free(title);
}

static void on_toggle_ask_yes_no_save_result_cb(GtkToggleButton *tb, gpointer user_data)
{
    set_user_setting(user_data, gtk_toggle_button_get_active(tb) ? "no" : "yes");
}

/*
 * Function shows a dialog with 'Yes/No' buttons and a check box allowing to
 * remeber the answer. The answer is stored in configuration file under
 * 'option_name' key.
 */
static bool ask_yes_no_save_result(const char *message, const char *option_name)
{
    const char *ask_result = get_user_setting(option_name);

    if (ask_result && string_to_bool(ask_result) == false)
        return true;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "%s", message);

    gint response = GTK_RESPONSE_CANCEL;
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(save_dialog_response), &response);

    GtkWidget *ask_yes_no_cb = gtk_check_button_new_with_label(_("Don't ask me again"));
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       ask_yes_no_cb, TRUE, TRUE, 0);
    g_signal_connect(ask_yes_no_cb, "toggled",
                     G_CALLBACK(on_toggle_ask_yes_no_save_result_cb), (gpointer)option_name);

    /* check it by default if it's shown for the first time */
    if (!ask_result)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ask_yes_no_cb), TRUE);

    gtk_widget_show(ask_yes_no_cb);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response == GTK_RESPONSE_YES;
}

static bool ask_continue_before_steal(const char *base_dir, const char *dump_dir)
{
    char *msg = xasprintf(_("Need writable directory, but '%s' is not writable."
                            " Move it to '%s' and operate on the moved data?"),
                            dump_dir, base_dir);
    const bool response = ask_yes_no_save_result(msg, "ask_steal_dir");
    free(msg);
    return response;
}

struct dump_dir *wizard_open_directory_for_writing(const char *dump_dir_name)
{
    struct dump_dir *dd = open_directory_for_writing(dump_dir_name,
                                                     ask_continue_before_steal);

    if (dd && strcmp(g_dump_dir_name, dd->dd_dirname) != 0)
    {
        char *old_name = g_dump_dir_name;
        g_dump_dir_name = xstrdup(dd->dd_dirname);
        update_window_title();
        free(old_name);
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
    g_hash_table_insert(g_loaded_texts, (gpointer)name, (gpointer)1);

    GtkTextBuffer *tb = gtk_text_view_get_buffer(tv);

    const char *str = g_cd ? problem_data_get_content_or_NULL(g_cd, name) : NULL;
    /* Bad: will choke at any text with non-Unicode parts: */
    /* gtk_text_buffer_set_text(tb, (str ? str : ""), -1);*/
    /* Start torturing ourself instead: */

    GtkTextIter beg_iter, end_iter;
    gtk_text_buffer_get_iter_at_offset(tb, &beg_iter, 0);
    gtk_text_buffer_get_iter_at_offset(tb, &end_iter, -1);
    gtk_text_buffer_delete(tb, &beg_iter, &end_iter);

    if (!str)
        return;

    const gchar *end;
    while (!g_utf8_validate(str, -1, &end))
    {
        gtk_text_buffer_insert_at_cursor(tb, str, end - str);
        char buf[8];
        unsigned len = snprintf(buf, sizeof(buf), "<%02X>", (unsigned char)*end);
        gtk_text_buffer_insert_at_cursor(tb, buf, len);
        str = end + 1;
    }
    gtk_text_buffer_insert_at_cursor(tb, str, strlen(str));
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

    const char *old_value = g_cd ? problem_data_get_content_or_NULL(g_cd, name) : "";
    if (!old_value)
        old_value = "";
    if (strcmp(new_value, old_value) != 0)
    {
        struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
        if (dd)
            dd_save_text(dd, name, new_value);

//FIXME: else: what to do with still-unsaved data in the widget??
        dd_close(dd);
        problem_data_reload_from_dump_dir();
        update_gui_state_from_problem_data();
    }
}

static void save_text_from_text_view(GtkTextView *tv, const char *name)
{
    gchar *new_str = get_malloced_string_from_text_view(tv);
    save_text_if_changed(name, new_str);
    free(new_str);
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
    GList *urls = find_url_tokens(str);
    for (GList *u = urls; u; u = g_list_next(u))
    {
        const struct url_token *const t = (struct url_token *)u->data;
        if (last < t->start)
            gtk_text_buffer_insert(tb, &text_iter, last, t->start - last);

        GtkTextTag *tag;
        tag = gtk_text_buffer_create_tag (tb, NULL, "foreground", "blue",
                                          "underline", PANGO_UNDERLINE_SINGLE, NULL);
        char *url = xstrndup(t->start, t->len);
        g_object_set_data (G_OBJECT (tag), "url", url);

        gtk_text_buffer_insert_with_tags(tb, &text_iter, url, -1, tag, NULL);

        last = t->start + t->len;
    }

    g_list_free_full(urls, g_free);

    if (last[0] != '\0')
        gtk_text_buffer_insert(tb, &text_iter, last, strlen(last));

    /* Scroll so that the end of the log is visible */
    gtk_text_view_scroll_to_iter(tv, &text_iter,
                /*within_margin:*/ 0.0, /*use_align:*/ FALSE, /*xalign:*/ 0, /*yalign:*/ 0);
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
            if (fork() != 0)
            {
                execlp("gnome-open", "gnome-open", url, NULL);
                execlp("xdg-open", "xdg-open", url, NULL);
                exit(1);
            }
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
            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW (text_view));
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
    GdkDeviceManager *device_manager = gdk_display_get_device_manager(gdk_window_get_display (win));
    GdkDevice *pointer = gdk_device_manager_get_client_pointer(device_manager);
    gdk_window_get_device_position(gtk_widget_get_window(text_view), pointer, &wx, &wy, NULL);

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          wx, wy, &bx, &by);

    set_cursor_if_appropriate(GTK_TEXT_VIEW (text_view), bx, by);

    return FALSE;
}

/* event_gui_data_t */

static event_gui_data_t *new_event_gui_data_t(void)
{
    return xzalloc(sizeof(event_gui_data_t));
}

static void free_event_gui_data_t(event_gui_data_t *evdata, void *unused)
{
    if (evdata)
    {
        free(evdata->event_name);
        free(evdata);
    }
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
    gchar *item_name;
    struct problem_item *item = get_current_problem_item_or_NULL(tree_view, &item_name);
    if (!item || !(item->flags & CD_FLAG_TXT))
        goto ret;
    if (!strchr(item->content, '\n')) /* one line? */
        goto ret; /* yes */

    gint exitcode;
    gchar *arg[3];
    arg[0] = (char *) "xdg-open";
    arg[1] = concat_path_file(g_dump_dir_name, item_name);
    arg[2] = NULL;

    g_spawn_sync(NULL, arg, NULL,
                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                 NULL, NULL, NULL, NULL, &exitcode, NULL);

    if (exitcode != EXIT_SUCCESS)
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("View/edit a text file"),
            GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            NULL, NULL);
        GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *textview = gtk_text_view_new();

        gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_SAVE, GTK_RESPONSE_OK);
        gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
        gtk_widget_set_size_request(scrolled, 640, 480);
        gtk_widget_show(scrolled);

        gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), textview);
        gtk_widget_show(textview);

        load_text_to_text_view(GTK_TEXT_VIEW(textview), item_name);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
            save_text_from_text_view(GTK_TEXT_VIEW(textview), item_name);

        gtk_widget_destroy(textview);
        gtk_widget_destroy(scrolled);
        gtk_widget_destroy(dialog);
    }

    free(arg[1]);
 ret:
    g_free(item_name);
}

/* static gboolean tv_details_select_cursor_row(
                        GtkTreeView *tree_view,
                        gboolean arg1,
                        gpointer user_data) {...} */

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

    gchar *item_name = NULL;
    struct problem_item *item = get_current_problem_item_or_NULL(tree_view, &item_name);
    g_free(item_name);

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
    //log("%s: path:'%s'", __func__, tree_path);
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
    g_free(item_name);
    if (!item) /* paranoia */
        return;

    int cur_value;
    if (item->selected_by_user == 0)
        cur_value = item->default_by_reporter;
    else
        cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */
    //log("%s: allowed:%d reqd:%d def:%d user:%d cur:%d", __func__,
    //            item->allowed_by_reporter,
    //            item->required_by_reporter,
    //            item->default_by_reporter,
    //            item->selected_by_user,
    //            cur_value
    //);
    if (item->allowed_by_reporter && !item->required_by_reporter)
    {
        cur_value = !cur_value;
        item->selected_by_user = cur_value * 2 - 1; /* map 0,1 to -1,1 */
        //log("%s: now ->selected_by_user=%d", __func__, item->selected_by_user);
        gtk_list_store_set(g_ls_details, &iter,
                DETAIL_COLUMN_CHECKBOX, cur_value,
                -1);
    }
}


/* update_gui_state_from_problem_data */

static gint find_by_button(gconstpointer a, gconstpointer button)
{
    const event_gui_data_t *evdata = a;
    return (evdata->toggle_button != button);
}

static int check_event_config(const char *event_name)
{
    GHashTable *errors = validate_event(event_name);
    if (errors != NULL)
    {
        g_hash_table_unref(errors);
        show_event_opt_error_dialog(event_name);
        return 1;
    }
    return 0;
}

static void event_rb_was_toggled(GtkButton *button, gpointer user_data)
{
    /* Note: called both when item is selected and _unselected_,
     * use gtk_toggle_button_get_active() to determine state.
     */
    GList *found = g_list_find_custom(g_list_events, button, find_by_button);
    if (found)
    {
        event_gui_data_t *evdata = found->data;
        if (gtk_toggle_button_get_active(evdata->toggle_button))
        {
            free(g_event_selected);
            g_event_selected = xstrdup(evdata->event_name);
            check_event_config(evdata->event_name);

            clear_warnings();
            const bool good_rating = check_minimal_bt_rating(g_event_selected);
            show_warnings();

            gtk_widget_set_sensitive(g_btn_next, good_rating);
        }
    }
}

/* event_name contains "EVENT1\nEVENT2\nEVENT3\n".
 * Add new {radio/check}buttons to GtkBox for each EVENTn (type depends on bool radio).
 * Remember them in GList **p_event_list (list of event_gui_data_t's).
 * Set "toggled" callback on each button to given GCallback if it's not NULL.
 * Return active button (or NULL if none created).
 */
/* helper */
static char *missing_items_in_comma_list(const char *input_item_list)
{
    if (!input_item_list)
        return NULL;

    char *item_list = xstrdup(input_item_list);
    char *result = item_list;
    char *dst = item_list;

    while (item_list[0])
    {
        char *end = strchr(item_list, ',');
        if (end) *end = '\0';
        if (!problem_data_get_item_or_NULL(g_cd, item_list))
        {
            if (dst != result)
                *dst++ = ',';
            dst = stpcpy(dst, item_list);
        }
        if (!end)
            break;
        *end = ',';
        item_list = end + 1;
    }
    if (result == dst)
    {
        free(result);
        result = NULL;
    }
    return result;
}
static event_gui_data_t *add_event_buttons(GtkBox *box,
                GList **p_event_list,
                char *event_name,
                GCallback func)
{
    //VERB2 log("removing all buttons from box %p", box);
    gtk_container_foreach(GTK_CONTAINER(box), &remove_child_widget, NULL);
    g_list_foreach(*p_event_list, (GFunc)free_event_gui_data_t, NULL);
    g_list_free(*p_event_list);
    *p_event_list = NULL;

    g_black_event_count = 0;

    if (!event_name || !event_name[0])
    {
        GtkWidget *lbl = gtk_label_new(_("No reporting targets are defined for this problem. Check configuration in /etc/libreport/*"));
        gtk_misc_set_alignment(GTK_MISC(lbl), /*x*/ 0.0, /*y*/ 0.0);
        make_label_autowrap_on_resize(GTK_LABEL(lbl));
        gtk_box_pack_start(box, lbl, /*expand*/ true, /*fill*/ false, /*padding*/ 0);
        return NULL;
    }

    event_gui_data_t *first_button = NULL;
    event_gui_data_t *active_button = NULL;
    while (1)
    {
        if (!event_name || !event_name[0])
            break;

        char *event_name_end = strchr(event_name, '\n');
        *event_name_end = '\0';

        event_config_t *cfg = get_event_config(event_name);

        /* Form a pretty text representation of event */
        /* By default, use event name: */
        const char *event_screen_name = event_name;
        const char *event_description = NULL;
        char *tmp_description = NULL;
        bool red_choice = false;
        bool green_choice = false;
        if (cfg)
        {
            /* .xml has (presumably) prettier description, use it: */
            if (cfg->screen_name)
                event_screen_name = cfg->screen_name;
            event_description = cfg->description;

            char *missing = missing_items_in_comma_list(cfg->ec_requires_items);
            if (missing)
            {
                red_choice = true;
                event_description = tmp_description = xasprintf(_("(requires: %s)"), missing);
                free(missing);
            }
            else
            if (cfg->ec_creates_items)
            {
                if (problem_data_get_item_or_NULL(g_cd, cfg->ec_creates_items))
                {
                    char *missing = missing_items_in_comma_list(cfg->ec_creates_items);
                    if (missing)
                        free(missing);
                    else
                    {
                        green_choice = true;
                        event_description = tmp_description = xasprintf(_("(not needed, data already exist: %s)"), cfg->ec_creates_items);
                    }
                }
            }
        }
        if (!green_choice && !red_choice)
            g_black_event_count++;

        //VERB2 log("adding button '%s' to box %p", event_name, box);
        char *event_label = xasprintf("%s%s%s",
                        event_screen_name,
                        (event_description ? " - " : ""),
                        event_description ? event_description : ""
        );
        free(tmp_description);

        GtkWidget *button = gtk_radio_button_new_with_label_from_widget(
                        (first_button ? GTK_RADIO_BUTTON(first_button->toggle_button) : NULL),
                        event_label
                );
        free(event_label);

        if (green_choice || red_choice)
        {
            GtkWidget *child = gtk_bin_get_child(GTK_BIN(button));
            if (child)
            {
                static const GdkColor red = { .red = 0xffff };
                static const GdkColor green = { .green = 0x7fff };
                const GdkColor *color = (green_choice ? &green : &red);
                //gtk_widget_modify_text(button, GTK_STATE_NORMAL, color);
                gtk_widget_modify_fg(child, GTK_STATE_NORMAL, color);
                gtk_widget_modify_fg(child, GTK_STATE_ACTIVE, color);
                gtk_widget_modify_fg(child, GTK_STATE_PRELIGHT, color);
            }
        }

        if (func)
            g_signal_connect(G_OBJECT(button), "toggled", func, xstrdup(event_name));

        if (cfg && cfg->long_descr)
            gtk_widget_set_tooltip_text(button, cfg->long_descr);

        event_gui_data_t *event_gui_data = new_event_gui_data_t();
        event_gui_data->event_name = xstrdup(event_name);
        event_gui_data->toggle_button = GTK_TOGGLE_BUTTON(button);
        *p_event_list = g_list_append(*p_event_list, event_gui_data);

        if (!first_button)
            first_button = event_gui_data;

        if (!green_choice && !red_choice && !active_button)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), true);
            active_button = event_gui_data;
        }

        *event_name_end = '\n';
        event_name = event_name_end + 1;

        gtk_box_pack_start(box, button, /*expand*/ false, /*fill*/ false, /*padding*/ 0);
    }

    return active_button;
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
        //notebook_page->scrolled_window->text_view
        notebook_child = gtk_notebook_get_nth_page(g_notebook, i);
        tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        tab_lbl = gtk_notebook_get_tab_label(g_notebook, notebook_child);
        item_name = gtk_label_get_text(GTK_LABEL(tab_lbl));
        VERB1 log("saving: '%s'", item_name);

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
}

static void append_item_to_ls_details(gpointer name, gpointer value, gpointer data)
{
    problem_item *item = (problem_item*)value;
    struct cd_stats *stats = data;
    GtkTreeIter iter;

    gtk_list_store_append(g_ls_details, &iter);
    stats->filecount++;

    //FIXME: use the human-readable problem_item_format(item) instead of item->content.
    if (item->flags & CD_FLAG_TXT)
    {
        if (item->flags & CD_FLAG_ISEDITABLE)
        {
            GtkWidget *tab_lbl = gtk_label_new((char *)name);
            GtkWidget *tev = gtk_text_view_new();
            gtk_widget_modify_font(GTK_WIDGET(tev), g_monospace_font);
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
        stat(item->content, &statbuf);
        stats->filesize += statbuf.st_size;
        char *msg = xasprintf(_("(binary file, %llu bytes)"), (long long)statbuf.st_size);
        gtk_list_store_set(g_ls_details, &iter,
                              DETAIL_COLUMN_NAME, (char *)name,
                              DETAIL_COLUMN_VALUE, msg,
                              -1);
        free(msg);
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
static void update_ls_details_checkboxes(void)
{
    event_config_t *cfg = get_event_config(g_event_selected);
    //log("%s: event:'%s', cfg:'%p'", __func__, g_event_selected, cfg);
    GHashTableIter iter;
    char *name;
    struct problem_item *item;
    g_hash_table_iter_init(&iter, g_cd);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&item))
    {
        /* Decide whether item is allowed, required, and what's the default */
        item->allowed_by_reporter = 1;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_always))
                item->allowed_by_reporter = 0;
            if ((item->flags & CD_FLAG_BIN) && cfg->ec_exclude_binary_items)
                item->allowed_by_reporter = 0;
        }

        item->default_by_reporter = item->allowed_by_reporter;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_by_default))
                item->default_by_reporter = 0;
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_include_items_by_default))
                item->allowed_by_reporter = item->default_by_reporter = 1;
        }

        item->required_by_reporter = 0;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_requires_items))
                item->default_by_reporter = item->allowed_by_reporter = item->required_by_reporter = 1;
        }

        int cur_value;
        if (item->selected_by_user == 0)
            cur_value = item->default_by_reporter;
        else
            cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */

        //log("%s: '%s' allowed:%d reqd:%d def:%d user:%d", __func__, name,
        //    item->allowed_by_reporter,
        //    item->required_by_reporter,
        //    item->default_by_reporter,
        //    item->selected_by_user
        //);

        /* Find corresponding line and update checkbox */
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_details), &iter))
        {
            do {
                gchar *item_name = NULL;
                gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                            DETAIL_COLUMN_NAME, &item_name,
                            -1);
                if (!item_name) /* paranoia, should never happen */
                    continue;
                int differ = strcmp(name, item_name);
                g_free(item_name);
                if (differ)
                    continue;
                gtk_list_store_set(g_ls_details, &iter,
                        DETAIL_COLUMN_CHECKBOX, cur_value,
                        -1);
                //log("%s: changed gtk_list_store_set to %d", __func__, (item->allowed_by_reporter && item->selected_by_user >= 0));
                break;
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_details), &iter));
        }
    }
}

void update_gui_state_from_problem_data(void)
{
    update_window_title();
    remove_tabs_from_notebook(g_notebook);

    const char *reason = problem_data_get_content_or_NULL(g_cd, FILENAME_REASON);
    const char *not_reportable = problem_data_get_content_or_NULL(g_cd,
                                                                  FILENAME_NOT_REPORTABLE);

    char *t = xasprintf("%s%s%s",
                        not_reportable ?: "",
                        not_reportable ? ": " : "",
                        reason ?: _("(no description)"));

    gtk_label_set_text(g_lbl_cd_reason, t);
    free(t);

    gtk_list_store_clear(g_ls_details);
    struct cd_stats stats = { 0 };
    g_hash_table_foreach(g_cd, append_item_to_ls_details, &stats);
    char *msg = xasprintf(_("%llu bytes, %u files"), (long long)stats.filesize, stats.filecount);
    gtk_label_set_text(g_lbl_size, msg);
    free(msg);

    load_text_to_text_view(g_tv_comment, FILENAME_COMMENT);

    /* Update event radio buttons */
    event_gui_data_t *active_button = add_event_buttons(
                g_box_events,
                &g_list_events,
                g_events,
                G_CALLBACK(event_rb_was_toggled)
    );

    if (g_expert_mode)
    {
        /* Update the value of currently selected event */
        free(g_event_selected);
        g_event_selected = NULL;
        if (active_button)
        {
            g_event_selected = xstrdup(active_button->event_name);
        }
        VERB2 log("g_event_selected='%s'", g_event_selected);
    }

    /* We can't just do gtk_widget_show_all once in main:
     * We created new widgets (buttons). Need to make them visible.
     */
    gtk_widget_show_all(GTK_WIDGET(g_wnd_assistant));
}


/* start_event_run */

struct analyze_event_data
{
    struct run_event_state *run_state;
    const char *event_name;
    GList *env_list;
    GtkWidget *page_widget;
    GtkLabel *status_label;
    GtkTextView *tv_log;
    const char *success_msg;
    const char *error_msg;
    GIOChannel *channel;
    struct strbuf *event_log;
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
    struct strbuf *item_list = strbuf_new();
    const char *fmt = "%s";

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(g_ls_details), &iter))
    {
        do {
            gchar *item_name = NULL;
            gboolean checked = 0;
            gtk_tree_model_get(GTK_TREE_MODEL(g_ls_details), &iter,
                    DETAIL_COLUMN_NAME, &item_name,
                    DETAIL_COLUMN_CHECKBOX, &checked,
                    -1);
            if (!item_name) /* paranoia, should never happen */
                continue;
            if (!checked)
            {
                strbuf_append_strf(item_list, fmt, item_name);
                fmt = ",%s";
            }
            g_free(item_name);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(g_ls_details), &iter));
    }
    char *var = strbuf_free_nobuf(item_list);
    //log("EXCLUDE_FROM_REPORT='%s'", var);
    if (var)
    {
        xsetenv("EXCLUDE_FROM_REPORT", var);
        free(var);
    }
    else
        unsetenv("EXCLUDE_FROM_REPORT");
}

static int spawn_next_command_in_evd(struct analyze_event_data *evd)
{
    evd->env_list = export_event_config(evd->event_name);
    int r = spawn_next_command(evd->run_state, g_dump_dir_name, evd->event_name);
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
                strbuf_append_strf(evd->event_log, "%s%c %.*s",
                        iso_date_string(NULL),
                        delim[evd->event_log_state],
                        (int)(end - str), str
                );
                break;
            case LOGSTATE_MIDLINE:
                strbuf_append_strf(evd->event_log, "%.*s", (int)(end - str), str);
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
    char *event_log = dd_load_text_ext(dd, FILENAME_EVENT_LOG, DD_FAIL_QUIETLY_ENOENT);

    /* Append new log part to existing log */
    unsigned len = strlen(event_log);
    if (len != 0 && event_log[len - 1] != '\n')
        event_log = append_to_malloced_string(event_log, "\n");
    event_log = append_to_malloced_string(event_log, str);

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
    free(event_log);
    dd_close(dd);
}

static void on_btn_cancel_event(GtkButton *button)
{
    if (g_event_child_pid > 0)
        kill(- g_event_child_pid, SIGTERM);
}

static bool is_processing_finished()
{
    return !g_expert_mode && !g_auto_event_list;
}

static void update_gui_on_finished_reporting()
{
    /* replace 'Forward' with 'Close' button */
    /* 1. hide next button */
    gtk_widget_hide(g_btn_next);
    /* 2. move close button to the last position */
    gtk_box_reorder_child(g_box_buttons, g_btn_close, 3);
}

static void terminate_event_chain()
{
    if (g_expert_mode)
        return;

    g_auto_event_list = NULL;
    update_gui_on_finished_reporting();
}

static void update_command_run_log(const char* message, struct analyze_event_data *evd)
{
    gtk_label_set_text(g_lbl_event_log, message);

    char *log_msg = xasprintf("%s\n", message);
    append_to_textview(evd->tv_log, log_msg);
    save_to_event_log(evd, log_msg);
    free(log_msg);
}

static void run_event_gtk_error(const char *error_line, void *param)
{
    update_command_run_log(error_line, (struct analyze_event_data *)param);
}

static char *run_event_gtk_logging(char *log_line, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;

    if (strcmp(log_line, "THANKYOU") == 0)
    {
        VERB1 log("Received a request for termination of processing of event chain. (Request: '%s')", log_line);
        terminate_event_chain();
        if (!g_expert_mode)
            evd->success_msg = _("Processing finished.");
    }
    else
        update_command_run_log(log_line, evd);

    return log_line;
}

static void log_request_response_communication(const char *request, const char *response, struct analyze_event_data *evd)
{
    char *message = xasprintf(response ? "%s '%s'" : "%s", request, response);
    update_command_run_log(message, evd);
    free(message);
}

static void run_event_gtk_alert(const char *msg, void *args)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "%s", msg);
    char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    log_request_response_communication(msg, NULL, (struct analyze_event_data *)args);
}

static char *ask_helper(const char *msg, void *args, bool password)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_OK_CANCEL,
            "%s", msg);
    char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *textbox = gtk_entry_new();
    /* gtk_entry_set_editable(GTK_ENTRY(textbox), TRUE);
     * is not available in gtk3, so please use the highlevel
     * g_object_set
     */
    g_object_set(G_OBJECT(textbox), "editable", TRUE, NULL);

    if (password)
        gtk_entry_set_visibility(GTK_ENTRY(textbox), FALSE);

    gtk_box_pack_start(GTK_BOX(vbox), textbox, TRUE, TRUE, 0);
    gtk_widget_show(textbox);

    char *response = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
    {
        const char *text = gtk_entry_get_text(GTK_ENTRY(textbox));
        response = xstrdup(text);
    }

    gtk_widget_destroy(textbox);
    gtk_widget_destroy(dialog);

    const char *log_response = "";
    if (response)
        log_response = password ? "********" : response;

    log_request_response_communication(msg, log_response, (struct analyze_event_data *)args);
    return response ? response : xstrdup("");
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
    char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    const int ret = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;

    gtk_widget_destroy(dialog);

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

static void start_event_run(const char *event_name,
                            GtkWidget *page,
                            GtkTextView *tv_log,
                            GtkLabel *status_label,
                            const char *start_msg,
                            const char *error_msg,
                            const char *success_msg);

static gboolean consume_cmd_output(GIOChannel *source, GIOCondition condition, gpointer data)
{
    struct analyze_event_data *evd = data;
    struct run_event_state *run_state = evd->run_state;

    const int retval = consume_event_command_output(run_state, g_dump_dir_name);

    if (retval < 0 && errno == EAGAIN)
        /* We got all buffered data, but fd is still open. Done for now */
        return TRUE; /* "please don't remove this event (yet)" */

    /* EOF/error */

    unexport_event_config(evd->env_list);
    evd->env_list = NULL;

    /* Make sure "Cancel" button won't send anything (process is gone) */
    g_event_child_pid = -1;
    evd->run_state->command_pid = -1; /* just for consistency */

    /* Write a final message to the log */
    if (evd->event_log->len != 0 && evd->event_log->buf[evd->event_log->len - 1] != '\n')
        save_to_event_log(evd, "\n");

    /* If program failed, or if it finished successfully without saying anything... */
    if (retval != 0 || evd->event_log_state == LOGSTATE_FIRSTLINE)
    {
        if (retval != 0) /* If program failed, emit error line */
            evd->event_log_state = LOGSTATE_ERRLINE;
        char *msg;
        if (WIFSIGNALED(run_state->process_status))
            msg = xasprintf("(killed by signal %u)\n", WTERMSIG(run_state->process_status));
        else
            msg = xasprintf("(exited with %u)\n", retval);
        append_to_textview(evd->tv_log, msg);
        save_to_event_log(evd, msg);
        free(msg);
    }

    /* Append log to FILENAME_EVENT_LOG */
    update_event_log_on_disk(evd->event_log->buf);
    strbuf_clear(evd->event_log);
    evd->event_log_state = LOGSTATE_FIRSTLINE;

    if (geteuid() == 0)
    {
        /* Reset mode/uig/gid to correct values for all files created by event run */
        struct dump_dir *dd = dd_opendir(g_dump_dir_name, 0);
        if (dd)
        {
            dd_sanitize_mode_and_owner(dd);
            dd_close(dd);
        }
    }

    /* Stop if exit code is not 0, or no more commands */
    if (retval != 0
     || spawn_next_command_in_evd(evd) < 0
    ) {
        VERB1 log("done running event on '%s': %d", g_dump_dir_name, retval);
        append_to_textview(evd->tv_log, "\n");

        /* Free child output buffer */
        strbuf_free(cmd_output);
        cmd_output = NULL;

        /* Hide spinner and stop btn */
        gtk_widget_hide(GTK_WIDGET(g_spinner_event_log));
        gtk_widget_hide(g_btn_stop);
        /* Enable (un-gray out) navigation buttons */
        gtk_widget_set_sensitive(g_btn_close, true);
        gtk_widget_set_sensitive(g_btn_next, true);

        problem_data_reload_from_dump_dir();
        update_gui_state_from_problem_data();

        if (retval)
        {
            gtk_label_set_text(evd->status_label, evd->error_msg);
            /* If we were running -e EV1 -e EV2, stop if EV1 failed: */
            terminate_event_chain();
        }
        else
            gtk_label_set_text(evd->status_label, evd->success_msg);

        /*g_source_remove(evd->event_source_id);*/
        close(evd->fd);
        free_run_event_state(evd->run_state);
        strbuf_free(evd->event_log);
        free(evd);

        /* Inform abrt-gui that it is a good idea to rescan the directory */
        kill(getppid(), SIGCHLD);

        if (is_processing_finished())
            update_gui_on_finished_reporting();
        else if (retval == 0 && !g_verbose && !g_expert_mode)
            on_next_btn_cb(GTK_WIDGET(g_btn_next), NULL);

        return FALSE; /* "please remove this event" */
    }

    /* New command was started. Continue waiting for input */

    /* Transplant cmd's output fd onto old one, so that main loop
     * is none the wiser that fd it waits on has changed
     */
    xmove_fd(evd->run_state->command_out_fd, evd->fd);
    evd->run_state->command_out_fd = evd->fd; /* just to keep it consistent */
    ndelay_on(evd->fd);

    /* Revive "Cancel" button */
    g_event_child_pid = evd->run_state->command_pid;

    return TRUE; /* "please don't remove this event (yet)" */
}

static void start_event_run(const char *event_name,
                GtkWidget *page,
                GtkTextView *tv_log,
                GtkLabel *status_label,
                const char *start_msg,
                const char *error_msg,
                const char *success_msg
) {
    /* Start event asynchronously on the dump dir
     * (synchronous run would freeze GUI until completion)
     */
    struct run_event_state *state = new_run_event_state();
    state->logging_callback = run_event_gtk_logging;
    state->error_callback = run_event_gtk_error;
    state->alert_callback = run_event_gtk_alert;
    state->ask_callback = run_event_gtk_ask;
    state->ask_yes_no_callback = run_event_gtk_ask_yes_no;
    state->ask_password_callback = run_event_gtk_ask_password;

    if (prepare_commands(state, g_dump_dir_name, event_name) == 0)
    {
 no_cmds:
        /* No commands needed?! (This is untypical) */
        free_run_event_state(state);
//TODO: better msg?
        char *msg = xasprintf(_("No processing for event '%s' is defined"), event_name);
        if (g_expert_mode)
            gtk_label_set_text(status_label, msg);
        else
        {
            gtk_label_set_text(status_label, error_msg);
            append_to_textview(tv_log, msg);
            terminate_event_chain();
        }
        free(msg);
        return;
    }

    struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
    dd_close(dd);
    if (!dd)
    {
        free_run_event_state(state);
        if (!g_expert_mode)
        {
            char *msg = xasprintf(_("Processing interrupted: can't continue without writable directory."));
            gtk_label_set_text(status_label, msg);
            free(msg);
            terminate_event_chain();
        }
        return; /* user refused to steal, or write error, etc... */
    }

    set_excluded_envvar();
    GList *env_list = export_event_config(event_name);

    if (spawn_next_command(state, g_dump_dir_name, event_name) < 0)
    {
        unexport_event_config(env_list);
        goto no_cmds;
    }
    g_event_child_pid = state->command_pid;

    /* At least one command is needed, and we started first one.
     * Hook its output fd to the main loop.
     */
    struct analyze_event_data *evd = xzalloc(sizeof(*evd));
    evd->run_state = state;
    evd->event_name = event_name;
    evd->env_list = env_list;
    evd->page_widget = page;
    evd->status_label = status_label;
    evd->tv_log = tv_log;
    evd->error_msg = error_msg;
    evd->success_msg = success_msg;
    evd->event_log = strbuf_new();
    evd->fd = state->command_out_fd;

    state->logging_param = evd;
    state->error_param = evd;
    state->interaction_param = evd;

    ndelay_on(evd->fd);
    evd->channel = g_io_channel_unix_new(evd->fd);
    /*evd->event_source_id = */ g_io_add_watch(evd->channel,
            G_IO_IN | G_IO_ERR | G_IO_HUP, /* need HUP to detect EOF w/o any data */
            consume_cmd_output,
            evd
    );

    gtk_label_set_text(status_label, start_msg);
    VERB1 log("running event '%s' on '%s'", event_name, g_dump_dir_name);
    char *msg = xasprintf("--- Running %s ---\n", event_name);
    append_to_textview(evd->tv_log, msg);
    free(msg);

    gtk_widget_show(GTK_WIDGET(g_spinner_event_log));
    gtk_widget_show(g_btn_stop);
    /* Disable (gray out) navigation buttons */
    gtk_widget_set_sensitive(g_btn_close, false);
    gtk_widget_set_sensitive(g_btn_next, false);
}


/* Backtrace checkbox handling */

static void add_warning(const char *warning)
{
    g_warning_issued = true;
    char *label_str = xasprintf("• %s", warning);
    GtkWidget *warning_lbl = gtk_label_new(label_str);
    /* should be safe to free it, gtk calls strdup() to copy it */
    free(label_str);

    gtk_misc_set_alignment(GTK_MISC(warning_lbl), 0.0, 0.0);
    gtk_label_set_justify(GTK_LABEL(warning_lbl), GTK_JUSTIFY_LEFT);
    gtk_box_pack_start(g_box_warning_labels, warning_lbl, false, false, 0);
    gtk_widget_show(warning_lbl);
}

static void show_warnings(void)
{
    if (g_warning_issued)
        gtk_widget_show(g_widget_warnings_area);
}

static void clear_warnings(void)
{
    /* erase all warnings */
    gtk_widget_hide(g_widget_warnings_area);
    gtk_container_foreach(GTK_CONTAINER(g_box_warning_labels), &remove_child_widget, NULL);
    g_warning_issued = false;
}

/* TODO : this function should not set a warning directly, it makes the function unusable for add_event_buttons(); */
static bool check_minimal_bt_rating(const char *event_name)
{
    bool acceptable_rating = true;
    event_config_t *event_cfg = NULL;

    if (!event_name)
        error_msg_and_die(_("Cannot check backtrace rating because of invalid event name"));
    else if (prefixcmp(event_name, "report") != 0)
    {
        VERB2 log("No checks for bactrace rating because event '%s' doesn't report.", event_name);
        return acceptable_rating;
    }
    else
        event_cfg = get_event_config(event_name);

    char *description = NULL;
    acceptable_rating = check_problem_rating_usability(event_cfg, g_cd, &description, NULL);
    if (description)
    {
        add_warning(description);
        free(description);
    }

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

    bool good =
        gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(g_tv_comment)) >= 10
        || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_cb_no_comment));

    /* Allow next page only when the comment has at least 10 chars */
    gtk_widget_set_sensitive(g_btn_next, good);

    /* And show the eventbox with label */
    if (good)
        gtk_widget_hide(GTK_WIDGET(g_eb_comment));
    else
        gtk_widget_show(GTK_WIDGET(g_eb_comment));
}

static void on_comment_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    toggle_eb_comment();
}

static void on_no_comment_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    toggle_eb_comment();
}


static void on_show_event_list_cb(GtkWidget *button, gpointer user_data)
{
    show_events_list_dialog(GTK_WINDOW(g_wnd_assistant));
}

#if 0
static void log_ready_state(void)
{
    char buf[NUM_PAGES+1];
    for (int i = 0; i < NUM_PAGES; i++)
    {
        char ch = '_';
        if (pages[i].page_widget)
            ch = gtk_assistant_get_page_complete(g_assistant, pages[i].page_widget) ? '+' : '-';
        buf[i] = ch;
    }
    buf[NUM_PAGES] = 0;
    log("Completeness:[%s]", buf);
}
#endif

static gboolean highligh_word_in_tabs(const char *search_word, int flags)
{
    gboolean found = false;
    GtkTextBuffer *buffer;
    GtkTextIter start_find;
    GtkTextIter end_find;
    GtkTextIter start_match;
    GtkTextIter end_match;
    PangoAttrList *attrs;
    int offset = 0;

    if (flags & CLEAR_PREVIOUS_RESULT)
    {
        list_free_with_free(g_search_result_list);
        g_search_result_list = NULL;
        g_current_highlighted_word = 0;
        g_first_highlight = true;
    }

    gint n_pages = gtk_notebook_get_n_pages(g_notebook);
    int page = 0;
    for (page = 0; page < n_pages; page++)
    {
        //notebook_page->scrolled_window->text_view
        GtkWidget *notebook_child = gtk_notebook_get_nth_page(g_notebook, page);
        GtkTextView *tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        buffer = gtk_text_view_get_buffer(tev);
        gtk_text_buffer_get_start_iter(buffer, &start_find);
        gtk_text_buffer_get_end_iter(buffer, &end_find);
        GtkWidget *tab_lbl = gtk_notebook_get_tab_label(g_notebook, notebook_child);
        //reset previous results
        if (flags & CLEAR_PREVIOUS_RESULT)
        {
            gtk_text_buffer_remove_tag_by_name(buffer, "search_result_bg", &start_find, &end_find);
            gtk_text_buffer_remove_tag_by_name(buffer, "current_result_bg", &start_find, &end_find);
            attrs = gtk_label_get_attributes(GTK_LABEL(tab_lbl));
            gtk_label_set_attributes(GTK_LABEL(tab_lbl), NULL);
            pango_attr_list_unref(attrs); //If the result is zero, free the attribute list and the attributes it contains.
        }

        /* adds * instead of changing color - usable for gtk < 2.8 */
        /*
        char* lbl = xstrdup(gtk_label_get_text(GTK_LABEL(tab_lbl)));
        if (lbl[0] == '*') // lets hope we don't have elements name starting with *
        {
            gtk_label_set_text(GTK_LABEL(tab_lbl), lbl+1);
        }
        free(lbl);
        */

        bool lbl_set = false;

        if (strncmp(gtk_label_get_text(GTK_LABEL(tab_lbl)), "page 1", 5) == 0)
        {
            continue;
        }

        while (search_word && search_word[0] && gtk_text_iter_forward_search(&start_find, search_word,
                                     GTK_TEXT_SEARCH_TEXT_ONLY,
                                     &start_match,
                                     &end_match, NULL))
        {
            search_item_t * found_word = (search_item_t *)xmalloc(sizeof(search_item_t));
            found_word->start = start_match;
            found_word->end = end_match;
            found_word->buffer = buffer;
            found_word->tev = tev;
            found_word->page = page;
            g_search_result_list = g_list_append(g_search_result_list, found_word);
            found = true;
            if (!lbl_set)
            {
                attrs = pango_attr_list_new();
                PangoAttribute *foreground_attr = pango_attr_foreground_new(65535, 0, 0);
                pango_attr_list_insert(attrs, foreground_attr);
                gtk_label_set_attributes(GTK_LABEL(tab_lbl), attrs);

                /* adds * instead of changing color - usable for gtk < 2.8 */
                /*
                char *found_lbl = xasprintf("*%s", gtk_label_get_text(GTK_LABEL(tab_lbl)));
                gtk_label_set_text(GTK_LABEL(tab_lbl), found_lbl);
                free(found_lbl);
                */

                lbl_set = true;
            }
            gtk_text_buffer_apply_tag_by_name(buffer, "search_result_bg",
                                              &start_match, &end_match);
            offset = gtk_text_iter_get_offset(&end_match);
            gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
        }
    }
    return found;
}

static void highlight_forbidden(void)
{
    gboolean found = false;
    GList *forbidden_words = load_forbidden_words();
    GList *cur_word = forbidden_words;
    while (cur_word && ((char *)cur_word->data)[0])
    {
        if(highligh_word_in_tabs((char *)cur_word->data, 0))
            found = true;
        cur_word = g_list_next(cur_word);
    }
    if (found)
    {
        add_warning(_("Possible sensitive data detected, please review the highlighted tabs carefully."));
    }
    list_free_with_free(forbidden_words);
}

static gint select_next_page_no(gint current_page_no, gpointer data);

static void setup_and_start_even_run(const char *event_name)
{
    start_event_run(event_name,
            pages[PAGENO_EVENT_PROGRESS].page_widget,
            g_tv_event_log,
            g_lbl_event_log,
            _("Processing..."),
            g_expert_mode ? _("Processing failed. You can try another operation if available.")
                          : _("Processing failed."),
            /* this event is the last event from the chain */
            is_processing_finished() ? _("Processing finished.")
                                     : _("Processing finished, please proceed to the next step.")
    );
}

static const char *get_next_processed_event(GList **events_list)
{
    if (!events_list || !*events_list)
        return NULL;

    const char *event_name = (const char *)(*events_list)->data;

    clear_warnings();
    const bool acceptable = check_minimal_bt_rating(event_name);
    show_warnings();

    if (!acceptable)
    {
        *events_list = NULL;
        return NULL;
    }

    *events_list = g_list_next(*events_list);
    return event_name;
}

static const char *setup_next_processed_event(GList **events_list)
{
    const char *event = get_next_processed_event(&g_auto_event_list);
    if (!event)
    {
        free(g_event_selected);
        g_event_selected = NULL;
        /* No next event, go to progress page and finish */
        gtk_label_set_text(g_lbl_event_log, _("Processing finished."));
        update_gui_on_finished_reporting();
        return NULL;
    }

    VERB1 log("selected -e EVENT:%s", event);
    return event;
}

static void on_page_prepare(GtkNotebook *assistant, GtkWidget *page, gpointer user_data)
{
    //int page_no = gtk_assistant_get_current_page(g_assistant);
    //log_ready_state();

    /* This suppresses [Last] button: assistant thinks that
     * we never have this page ready unless we are on it
     * -> therefore there is at least one non-ready page
     * -> therefore it won't show [Last]
     */
    // Doesn't work: if Completeness:[++++++-+++],
    // then [Last] btn will still be shown.
    //gtk_assistant_set_page_complete(g_assistant,
    //            pages[PAGENO_REVIEW_DATA].page_widget,
    //            pages[PAGENO_REVIEW_DATA].page_widget == page
    //);

    if (pages[PAGENO_SUMMARY].page_widget == page)
    {
        if (!g_expert_mode)
        {
            /* Skip intro screen */
            int n = select_next_page_no(pages[PAGENO_SUMMARY].page_no, NULL);
            VERB2 log("switching to page_no:%d", n);
            gtk_notebook_set_current_page(assistant, n);
            return;
        }
    }

    if (pages[PAGENO_EDIT_ELEMENTS].page_widget == page)
    {
        clear_warnings();
        highlight_forbidden();
        show_warnings();
    }

    /* Save text fields if changed */
    save_items_from_notepad();
    save_text_from_text_view(g_tv_comment, FILENAME_COMMENT);

    if (pages[PAGENO_SUMMARY].page_widget == page
     || pages[PAGENO_REVIEW_DATA].page_widget == page
    ) {
        GtkWidget *w = GTK_WIDGET(g_tv_details);
        GtkContainer *c = GTK_CONTAINER(gtk_widget_get_parent(w));
        if (c)
            gtk_container_remove(c, w);
        gtk_container_add(pages[PAGENO_SUMMARY].page_widget == page ?
                        g_container_details1 : g_container_details2,
                w
        );
        /* Make checkbox column visible only on the last page */
        gtk_tree_view_column_set_visible(g_tv_details_col_checkbox,
                (pages[PAGENO_REVIEW_DATA].page_widget == page)
        );

        if (pages[PAGENO_REVIEW_DATA].page_widget == page)
        {
            gtk_widget_set_sensitive(g_btn_next, gtk_toggle_button_get_active(g_tb_approve_bt));
            update_ls_details_checkboxes();
        }
    }

    if (pages[PAGENO_EDIT_COMMENT].page_widget == page)
    {
        gtk_widget_set_sensitive(g_btn_next, false);
        on_comment_changed(gtk_text_view_get_buffer(g_tv_comment), NULL);
    }
    //log_ready_state();

    if (pages[PAGENO_EVENT_PROGRESS].page_widget == page)
    {
        VERB2 log("g_event_selected:'%s'", g_event_selected);
        if (g_event_selected
         && g_event_selected[0]
        ) {
            clear_warnings();
            setup_and_start_even_run(g_event_selected);
        }
    }
}

static bool get_sensitive_data_permission(const char *event_name)
{
    event_config_t *event_cfg = get_event_config(event_name);

    if (!event_cfg || !event_cfg->ec_sending_sensitive_data)
        return true;

    char *msg = xasprintf(_("Event '%s' requires permission to send possibly sensitive data."
                            "\nDo you want to continue?"),
                            event_cfg->screen_name ? event_cfg->screen_name : event_name);
    const bool response = ask_yes_no_save_result(msg, "ask_send_sensitive_data");
    free(msg);

    return response;
}

static gint select_next_page_no(gint current_page_no, gpointer data)
{
    GtkWidget *page;

 again:
    VERB1 log("%s: current_page_no:%d", __func__, current_page_no);
    current_page_no++;
    page = gtk_notebook_get_nth_page(g_assistant, current_page_no);

    if (pages[PAGENO_EVENT_SELECTOR].page_widget == page)
    {
        if (!g_expert_mode)
        {
            const char *event = setup_next_processed_event(&g_auto_event_list);
            if (!event)
            {
                current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
                goto again;
            }

            free(g_event_selected);

            if (!get_sensitive_data_permission(event))
            {
                g_event_selected = NULL;
                gtk_label_set_text(g_lbl_event_log, _("Processing was cancelled"));
                terminate_event_chain();
                current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
                goto again;
            }

            g_event_selected = xstrdup(event);

            if (check_event_config(g_event_selected) != 0)
            {
                goto again;
            }

            current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no + 1;
            goto event_was_selected;
        }
    }

    if (pages[PAGENO_EVENT_SELECTOR + 1].page_widget == page)
    {
 event_was_selected:
        if (!g_event_selected)
        {
            /* Go back to selectors */
            current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no - 1;
            goto again;
        }

        if (!event_need_review(g_event_selected))
        {
            current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
            goto again;
        }
    }

#if 0
    if (pages[PAGENO_EDIT_COMMENT].page_widget == page)
    {
        if (problem_data_get_content_or_NULL(g_cd, FILENAME_COMMENT))
            goto again; /* no comment, skip this page */
    }
#endif

    if (pages[PAGENO_EVENT_DONE].page_widget == page)
    {
        if (g_auto_event_list)
        {
            /* Go back to selectors */
            current_page_no = pages[PAGENO_SUMMARY].page_no;
        }
        goto again;
    }

    if (pages[PAGENO_NOT_SHOWN].page_widget == page)
    {
        if (!g_expert_mode)
            exit(0);
        /* No! this would SEGV (infinitely recurse into select_next_page_no) */
        /*gtk_assistant_commit(g_assistant);*/
        current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no - 1;
        goto again;
    }

    VERB1 log("%s: selected page #%d", __func__, current_page_no);
    return current_page_no;
}



static void highlight_widget(GtkWidget *widget, gpointer *user_data)
{
    gtk_drag_highlight(widget);
}

static void unhighlight_widget(GtkWidget *widget, gpointer *user_data)
{
    gtk_drag_unhighlight(widget);
}

static void unhighlight_current_word(void)
{
    search_item_t *word = NULL;
    word = (search_item_t *)g_list_nth_data(g_search_result_list, g_current_highlighted_word);
    if (word)
    {
        gtk_text_buffer_remove_tag_by_name(word->buffer, "current_result_bg", &(word->start), &(word->end));
    }
}

static void highlight_current_word(void)
{
    search_item_t *word = NULL;
    word = (search_item_t *)g_list_nth_data(g_search_result_list, g_current_highlighted_word);
    if (word)
    {
        gtk_notebook_set_current_page(g_notebook, word->page);
        gtk_text_buffer_apply_tag_by_name(word->buffer, "current_result_bg", &(word->start), &(word->end));
        gtk_text_buffer_place_cursor(word->buffer, &(word->start));
        gtk_text_view_scroll_to_iter(word->tev, &(word->start), 0.0, false, 0, 0);
    }
}

static void search_down(GtkWidget *widget, gpointer user_data)
{
    if (g_current_highlighted_word < g_list_length(g_search_result_list)-1)
    {
        unhighlight_current_word();
        if (!g_first_highlight)
            g_current_highlighted_word++;
        g_first_highlight = false;
        highlight_current_word();
    }
}

static void search_up(GtkWidget *widget, gpointer user_data)
{
    if (g_current_highlighted_word > 0)
    {
        unhighlight_current_word();
        if (!g_first_highlight)
            g_current_highlighted_word--;
        g_first_highlight = false;
        highlight_current_word();
    }
}

static gboolean highlight_search(gpointer user_data)
{
    GtkEntry *entry = GTK_ENTRY(user_data);

    VERB1 log("searching: '%s'", gtk_entry_get_text(entry));

    highligh_word_in_tabs(gtk_entry_get_text(entry), CLEAR_PREVIOUS_RESULT);

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
    g_timeout = g_timeout_add(500, &highlight_search, (gpointer)entry);
}

static void save_edited_one_liner(GtkCellRendererText *renderer,
                gchar *tree_path,
                gchar *new_text,
                gpointer user_data)
{
    //log("path:'%s' new_text:'%s'", tree_path, new_text);

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
            item->content = xstrdup(new_text);
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
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
            NULL
    );
    char *filename = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    if (filename)
    {
        char *basename = strrchr(filename, '/');
        if (!basename)  /* wtf? (never happens) */
            goto free_and_ret;
        basename++;

        /* TODO: ask for the name to save it as? For now, just use basename */

        char *message = NULL;

        struct stat statbuf;
        if (stat(filename, &statbuf) != 0 || !S_ISREG(statbuf.st_mode))
        {
            message = xasprintf(_("'%s' is not an ordinary file"), filename);
            goto show_msgbox;
        }

        struct problem_item *item = problem_data_get_item_or_NULL(g_cd, basename);
        if (!item || (item->flags & CD_FLAG_ISEDITABLE))
        {
            struct dump_dir *dd = wizard_open_directory_for_writing(g_dump_dir_name);
            if (dd)
            {
                char *new_name = concat_path_file(g_dump_dir_name, basename);
                if (strcmp(filename, new_name) == 0)
                {
                    message = xstrdup(_("You are trying to copy a file onto itself"));
                }
                else
                {
                    off_t r = copy_file(filename, new_name, 0666);
                    if (r < 0)
                    {
                        message = xasprintf(_("Can't copy '%s': %s"), filename, strerror(errno));
                        unlink(new_name);
                    }
                    if (!message)
                    {
                        problem_data_reload_from_dump_dir();
                        update_gui_state_from_problem_data();
                        /* Set flags for the new item */
                        update_ls_details_checkboxes();
                    }
                }
                free(new_name);
            }
            dd_close(dd);
        }
        else
            message = xasprintf(_("Item '%s' already exists and is not modifiable"), basename);

        if (message)
        {
 show_msgbox: ;
            GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(g_wnd_assistant),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING,
                GTK_BUTTONS_CLOSE,
                message);
            free(message);
            gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(g_wnd_assistant));
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
        }
 free_and_ret:
        g_free(filename);
    }
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
                        char *filename = concat_path_file(g_dump_dir_name, item_name);
                        unlink(filename);
                        free(filename);
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


/* Initialization */

/* wizard.glade file as a string WIZARD_GLADE_CONTENTS: */
#include "wizard_glade.c"

static void on_next_btn_cb(GtkWidget *btn, gpointer user_data)
{
    gint current_page_no = gtk_notebook_get_current_page(g_assistant);
    gint next_page_no = select_next_page_no(current_page_no, NULL);

    /* if pageno is not change 'switch-page' signal is not emitted */
    if (current_page_no == next_page_no)
        on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, next_page_no), NULL);
    else
        gtk_notebook_set_current_page(g_assistant, next_page_no);
}

static void add_pages(void)
{
    GError *error = NULL;
    if (!g_glade_file)
    {
        /* Load UI from internal string */
        gtk_builder_add_objects_from_string(g_builder,
                WIZARD_GLADE_CONTENTS, sizeof(WIZARD_GLADE_CONTENTS) - 1,
                (gchar**)page_names,
                &error);
        if (error != NULL)
            error_msg_and_die("Error loading glade data: %s", error->message);
    }
    else
    {
        /* -g FILE: load IU from it */
        gtk_builder_add_objects_from_file(g_builder, g_glade_file, (gchar**)page_names, &error);
        if (error != NULL)
            error_msg_and_die("Can't load %s: %s", g_glade_file, error->message);
    }

    struct dump_dir *dd = dd_opendir(g_dump_dir_name, DD_OPEN_READONLY | DD_FAIL_QUIETLY_EACCES);
    if (!dd)
        xfunc_die();
    char *not_reportable = dd_load_text_ext(dd, FILENAME_NOT_REPORTABLE, 0
                                            | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE
                                            | DD_FAIL_QUIETLY_ENOENT
                                            | DD_FAIL_QUIETLY_EACCES);
    dd_close(dd);

    int i;
    int page_no = 0;
    for (i = 0; page_names[i] != NULL; i++)
    {
        GtkWidget *page = GTK_WIDGET(gtk_builder_get_object(g_builder, page_names[i]));
        pages[i].page_widget = page;
        pages[i].page_no = page_no++;
        gtk_notebook_append_page(g_assistant, page, gtk_label_new(pages[i].title));
        VERB1 log("added page: %s", page_names[i]);
    }
    free(not_reportable);

    /* Set pointers to objects we might need to work with */
    g_lbl_cd_reason        = GTK_LABEL(        gtk_builder_get_object(g_builder, "lbl_cd_reason"));
    g_box_events           = GTK_BOX(          gtk_builder_get_object(g_builder, "vb_events"));
    g_lbl_event_log        = GTK_LABEL(        gtk_builder_get_object(g_builder, "lbl_event_log"));
    g_tv_event_log         = GTK_TEXT_VIEW(    gtk_builder_get_object(g_builder, "tv_event_log"));
    g_tv_comment           = GTK_TEXT_VIEW(    gtk_builder_get_object(g_builder, "tv_comment"));
    g_eb_comment           = GTK_EVENT_BOX(    gtk_builder_get_object(g_builder, "eb_comment"));
    g_cb_no_comment        = GTK_CHECK_BUTTON( gtk_builder_get_object(g_builder, "cb_no_comment"));
    g_tv_details           = GTK_TREE_VIEW(    gtk_builder_get_object(g_builder, "tv_details"));
    g_tb_approve_bt        = GTK_TOGGLE_BUTTON(gtk_builder_get_object(g_builder, "cb_approve_bt"));
    g_search_entry_bt      = GTK_ENTRY(        gtk_builder_get_object(g_builder, "entry_search_bt"));
    g_container_details1   = GTK_CONTAINER(    gtk_builder_get_object(g_builder, "container_details1"));
    g_container_details2   = GTK_CONTAINER(    gtk_builder_get_object(g_builder, "container_details2"));
    g_btn_add_file         = GTK_BUTTON(       gtk_builder_get_object(g_builder, "btn_add_file"));
    g_lbl_size             = GTK_LABEL(        gtk_builder_get_object(g_builder, "lbl_size"));
    g_notebook             = GTK_NOTEBOOK(     gtk_builder_get_object(g_builder, "notebook_edit"));
    g_ev_search_up         = GTK_EVENT_BOX(    gtk_builder_get_object(g_builder, "ev_search_up"));
    g_ev_search_down       = GTK_EVENT_BOX(    gtk_builder_get_object(g_builder, "ev_search_down"));
    g_spinner_event_log    = GTK_SPINNER(      gtk_builder_get_object(g_builder, "spinner_event_log"));

    gtk_widget_set_no_show_all(GTK_WIDGET(g_spinner_event_log), true);

    gtk_widget_modify_font(GTK_WIDGET(g_tv_event_log), g_monospace_font);
    fix_all_wrapped_labels(GTK_WIDGET(g_assistant));

    /* Configure btn on select analyzers page */
    GtkWidget *config_btn = GTK_WIDGET(gtk_builder_get_object(g_builder, "button_cfg1"));
    if (config_btn)
        g_signal_connect(G_OBJECT(config_btn), "clicked", G_CALLBACK(on_show_event_list_cb), NULL);

    g_signal_connect(g_cb_no_comment, "toggled", G_CALLBACK(on_no_comment_toggled), NULL);

    /* hook up the search arrows */
    g_signal_connect(G_OBJECT(g_ev_search_up), "enter-notify-event", G_CALLBACK(highlight_widget), NULL);
    g_signal_connect(G_OBJECT(g_ev_search_up), "leave-notify-event", G_CALLBACK(unhighlight_widget), NULL);
    g_signal_connect(G_OBJECT(g_ev_search_up), "button-press-event", G_CALLBACK(search_up), NULL);

    g_signal_connect(G_OBJECT(g_ev_search_down), "enter-notify-event", G_CALLBACK(highlight_widget), NULL);
    g_signal_connect(G_OBJECT(g_ev_search_down), "leave-notify-event", G_CALLBACK(unhighlight_widget), NULL);
    g_signal_connect(G_OBJECT(g_ev_search_down), "button-press-event", G_CALLBACK(search_down), NULL);

    /* Set color of the comment evenbox */
    GdkColor color;
    gdk_color_parse("#CC3333", &color);
    gtk_widget_modify_bg(GTK_WIDGET(g_eb_comment), GTK_STATE_NORMAL, &color);

    g_signal_connect(g_tv_details, "key-press-event", G_CALLBACK(on_key_press_event_in_item_list), NULL);
}

static void create_details_treeview(void)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_toggle_new();
    g_tv_details_col_checkbox = column = gtk_tree_view_column_new_with_attributes(
                _("Include"), renderer,
                /* which "attr" of renderer to set from which COLUMN? (can be repeated) */
                "active", DETAIL_COLUMN_CHECKBOX,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);
    /* This column has a handler */
    g_signal_connect(renderer, "toggled", G_CALLBACK(g_tv_details_checkbox_toggled), NULL);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Name"), renderer,
                "text", DETAIL_COLUMN_NAME,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);

    g_tv_details_renderer_value = renderer = gtk_cell_renderer_text_new();
    g_signal_connect(renderer, "edited", G_CALLBACK(save_edited_one_liner), NULL);
    column = gtk_tree_view_column_new_with_attributes(
                _("Value"), renderer,
                "text", DETAIL_COLUMN_VALUE,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);

    /*
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Path"), renderer,
                "text", DETAIL_COLUMN_PATH,
                NULL);
    gtk_tree_view_append_column(g_tv_details, column);
    */

    gtk_tree_view_column_set_sort_column_id(column, DETAIL_COLUMN_NAME);

    g_ls_details = gtk_list_store_new(DETAIL_NUM_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(g_tv_details, GTK_TREE_MODEL(g_ls_details));

    g_signal_connect(g_tv_details, "row-activated", G_CALLBACK(tv_details_row_activated), NULL);
    g_signal_connect(g_tv_details, "cursor-changed", G_CALLBACK(tv_details_cursor_changed), NULL);
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
    init_page(&pages[1], PAGE_EVENT_SELECTOR     , _("Select operation")      );
    init_page(&pages[2], PAGE_EDIT_COMMENT,_("Provide additional information"));
    init_page(&pages[3], PAGE_EDIT_ELEMENTS      , _("Review the data")       );
    init_page(&pages[4], PAGE_REVIEW_DATA        , _("Confirm data to report"));
    init_page(&pages[5], PAGE_EVENT_PROGRESS     , _("Processing")            );
    init_page(&pages[6], PAGE_EVENT_DONE         , _("Processing done")       );
//do we still need this?
    init_page(&pages[7], PAGE_NOT_SHOWN          , ""                         );
}

static void assistant_quit_cb(void *obj, void *data)
{
    g_hash_table_destroy(g_loaded_texts);
    gtk_main_quit();
}

void create_assistant(void)
{
    g_loaded_texts = g_hash_table_new(g_str_hash, g_str_equal);

    g_expert_mode = !g_auto_event_list;

    g_monospace_font = pango_font_description_from_string("monospace");
    g_builder = gtk_builder_new();

    g_assistant = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_show_tabs(g_assistant, (g_verbose != 0));

    g_btn_close = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    g_btn_stop = gtk_button_new_from_stock(GTK_STOCK_STOP);
    gtk_widget_set_no_show_all(g_btn_stop, true); /* else gtk_widget_hide won't work */
    g_btn_next = gtk_button_new_from_stock(GTK_STOCK_GO_FORWARD);
    gtk_widget_set_no_show_all(g_btn_next, true); /* else gtk_widget_hide won't work */

    g_box_buttons = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_box_pack_start(g_box_buttons, g_btn_close, false, false, 5);
    gtk_box_pack_start(g_box_buttons, g_btn_stop, false, false, 5);
    /* Btns above are to the left, the rest are to the right: */
    GtkWidget *w = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
    gtk_box_pack_start(g_box_buttons, w, true, true, 5);
    gtk_box_pack_start(g_box_buttons, g_btn_next, false, false, 5);

    {   /* Warnings area widget definition start */
        GtkWidget *alignment_left = gtk_alignment_new(0.5,0.5,1,1);
        gtk_widget_set_visible(alignment_left, TRUE);

        GtkWidget *alignment_right = gtk_alignment_new(0.5,0.5,1,1);
        gtk_widget_set_visible(alignment_right, TRUE);

        g_box_warning_labels = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(g_box_warning_labels), TRUE);

        GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(vbox), TRUE);
        gtk_box_pack_start(vbox, GTK_WIDGET(g_box_warning_labels), false, false, 5);

        GtkWidget *image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_visible(image, TRUE);

        g_widget_warnings_area = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_set_visible(g_widget_warnings_area, FALSE);
        gtk_widget_set_no_show_all(g_widget_warnings_area, TRUE);
        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), alignment_left, true, false, 0);
        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), image, false, false, 5);
        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), GTK_WIDGET(vbox), false, false, 0);
        gtk_box_pack_start(GTK_BOX(g_widget_warnings_area), alignment_right, true, false, 0);
    }   /* Warnings area widget definition end */

    g_box_assistant = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_assistant), true, true, 0);

    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_widget_warnings_area), false, false, 0);
    gtk_box_pack_start(g_box_assistant, GTK_WIDGET(g_box_buttons), false, false, 5);

    gtk_widget_show_all(GTK_WIDGET(g_box_buttons));
    gtk_widget_hide(g_btn_stop);
    gtk_widget_show(g_btn_next);

    g_wnd_assistant = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_container_add(GTK_CONTAINER(g_wnd_assistant), GTK_WIDGET(g_box_assistant));

    gtk_window_set_default_size(g_wnd_assistant, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    /* set_default sets icon for every windows used in this app, so we don't
     * have to set the icon for those windows manually
     */
    gtk_window_set_default_icon_name("abrt");

    init_pages();

    add_pages();

    create_details_treeview();

    g_signal_connect(g_btn_close, "clicked", G_CALLBACK(assistant_quit_cb), NULL);
    g_signal_connect(g_btn_stop, "clicked", G_CALLBACK(on_btn_cancel_event), NULL);
    g_signal_connect(g_btn_next, "clicked", G_CALLBACK(on_next_btn_cb), NULL);

    g_signal_connect(g_wnd_assistant, "destroy", G_CALLBACK(assistant_quit_cb), NULL);
    g_signal_connect(g_assistant, "switch-page", G_CALLBACK(on_page_prepare), NULL);

    g_signal_connect(g_tb_approve_bt, "toggled", G_CALLBACK(on_bt_approve_toggle), NULL);
    g_signal_connect(gtk_text_view_get_buffer(g_tv_comment), "changed", G_CALLBACK(on_comment_changed), NULL);

    g_signal_connect(g_btn_add_file, "clicked", G_CALLBACK(on_btn_add_file), NULL);

    g_signal_connect(g_search_entry_bt, "changed", G_CALLBACK(search_timeout), NULL);

    g_signal_connect (g_tv_event_log, "key-press-event", G_CALLBACK (key_press_event), NULL);
    g_signal_connect (g_tv_event_log, "event-after", G_CALLBACK (event_after), NULL);
    g_signal_connect (g_tv_event_log, "motion-notify-event", G_CALLBACK (motion_notify_event), NULL);
    g_signal_connect (g_tv_event_log, "visibility-notify-event", G_CALLBACK (visibility_notify_event), NULL);

    hand_cursor = gdk_cursor_new (GDK_HAND2);
    regular_cursor = gdk_cursor_new (GDK_XTERM);

    /* switch to right starting page */
    on_page_prepare(g_assistant, gtk_notebook_get_nth_page(g_assistant, 0), NULL);
}
