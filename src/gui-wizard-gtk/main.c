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
#include "internal_libreport.h"
#include "wizard.h"
#if HAVE_LOCALE_H
# include <locale.h>
#endif

char *g_glade_file = NULL;
char *g_dump_dir_name = NULL;
char *g_events = NULL;
GList *g_auto_event_list = NULL;
problem_data_t *g_cd;


void problem_data_reload_from_dump_dir(void)
{
    free(g_events);

    struct dump_dir *dd = dd_opendir(g_dump_dir_name, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die(); /* dd_opendir already logged error msg */

    problem_data_t *new_cd = create_problem_data_from_dump_dir(dd);
    problem_data_add_text_noteditable(new_cd, CD_DUMPDIR, g_dump_dir_name);

    g_events = list_possible_events(dd, NULL, "");
    dd_close(dd);

    /* Copy "selected for reporting" flags */
    GHashTableIter iter;
    char *name;
    struct problem_item *new_item;
    g_hash_table_iter_init(&iter, new_cd);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&new_item))
    {
        struct problem_item *old_item = g_cd ? problem_data_get_item_or_NULL(g_cd, name) : NULL;
        if (old_item)
        {
            new_item->selected_by_user = old_item->selected_by_user;
            new_item->allowed_by_reporter = old_item->allowed_by_reporter;
            new_item->default_by_reporter = old_item->default_by_reporter;
            new_item->required_by_reporter = old_item->required_by_reporter;
        }
        else
        {
            new_item->selected_by_user = 0;
            new_item->allowed_by_reporter = 0;
            new_item->default_by_reporter = 0;
            new_item->required_by_reporter = 0;
        }
        //log("%s: was ->selected_by_user=%d", __func__, new_item->selected_by_user);
    }
    problem_data_free(g_cd);
    g_cd = new_cd;
}

static void
preferences_activated(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data)
{
    GtkApplication *app = GTK_APPLICATION(data);
    show_config_list_dialog(GTK_WINDOW(gtk_application_get_active_window(app)));
}

static void
quit_activated(GSimpleAction *action,
                           GVariant *parameter,
                           gpointer data)
{
    g_application_quit(G_APPLICATION(data));
}

static GActionEntry app_entries[] =
{
    { "preferences", preferences_activated, NULL, NULL, NULL },
    { "quit", quit_activated, NULL, NULL, NULL }
};

static void
startup_wizard(GApplication *app,
                gpointer user_data)
{
    g_action_map_add_action_entries(G_ACTION_MAP (app),
            app_entries, G_N_ELEMENTS (app_entries),
            app);

    GMenu *app_menu = g_menu_new();
    g_menu_append(app_menu, _("Preferences"), "app.preferences");

    GMenu *service_app_menu_sec = g_menu_new();
    g_menu_append(service_app_menu_sec, _("Quit"), "app.quit");
    g_menu_append_section(app_menu, /*no title*/NULL, G_MENU_MODEL(service_app_menu_sec));

    gtk_application_set_app_menu (GTK_APPLICATION (app), G_MENU_MODEL(app_menu));
}

static void
activate_wizard(GApplication *app,
                gpointer user_data)
{
    create_assistant(GTK_APPLICATION(app), *(int *)user_data);
    update_gui_state_from_problem_data(UPDATE_SELECTED_EVENT);
}

int main(int argc, char **argv)
{
    int expert_mode = 0;

    const char *prgname = "abrt";
    abrt_init(argv);

    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    /* without this the name is set to argv[0] which confuses
     * desktops which uses the name to find the corresponding .desktop file
     * trac#180
     *
     * env variable can be used to override the default prgname, so it's the
     * same as the application which is calling us (trac#303)
     *
     * note that g_set_prgname has to be called before gtk_init
     */
    char *env_prgname = getenv("LIBREPORT_PRGNAME");
    g_set_prgname(env_prgname ? env_prgname : prgname);

    gtk_init(&argc, &argv);

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "& [-vpdx] [-e EVENT]... [-g GUI_FILE] PROBLEM_DIR\n"
        "\n"
        "GUI tool to analyze and report problem saved in specified PROBLEM_DIR"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_g = 1 << 1,
        OPT_p = 1 << 2,
        OPT_d = 1 << 3,
        OPT_e = 1 << 4,
        OPT_x = 1 << 5,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('g', NULL, &g_glade_file, "FILE",          _("Alternate GUI file")),
        OPT_BOOL(  'p', NULL, NULL,                           _("Add program names to log")),
        OPT_BOOL(  'd', "delete", NULL,                       _("Remove PROBLEM_DIR after reporting")),
        OPT_LIST(  'e', "event", &g_auto_event_list, "EVENT", _("Run only these events")),
        OPT_BOOL(  'x', "expert", &expert_mode,               _("Expert mode")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;
    if (!argv[0] || argv[1]) /* zero or >1 arguments */
        show_usage_and_die(program_usage_string, program_options);

    /* Allow algorithms to add mallocated strings */
    for (GList *elem = g_auto_event_list; elem; elem = g_list_next(elem))
        elem->data = xstrdup((const char *)elem->data);

    export_abrt_envvars(opts & OPT_p);

    g_dump_dir_name = xstrdup(argv[0]);

    /* load /etc/abrt/events/foo.{conf,xml} stuff
       and $XDG_CACHE_HOME/abrt/events/foo.conf */
    g_event_config_list = load_event_config_data();
    load_event_config_data_from_user_storage(g_event_config_list);
    load_user_settings("report-gtk");

    load_workflow_config_data(WORKFLOWS_DIR);

    /* list of workflows applicable to the currently processed problem */
    GHashTable *possible_workflows = load_workflow_config_data_from_list(
                list_possible_events_glist(g_dump_dir_name, "workflow"),
                WORKFLOWS_DIR);

    /* if we have only 1 workflow, we can use the events from it as default */
    if (!expert_mode && g_auto_event_list == NULL && g_hash_table_size(possible_workflows) == 1)
    {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init(&iter, possible_workflows);
        if (g_hash_table_iter_next(&iter, &key, &value))
        {
            log_notice("autoselected workflow: '%s'", (char *)key);
            g_auto_event_list = wf_get_event_names((workflow_t *)value);
        }
    }

    problem_data_reload_from_dump_dir();

    g_custom_logger = &show_error_as_msgbox;
    GtkApplication *app = gtk_application_new("org.freedesktop.libreport.report", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate_wizard), (gpointer)&expert_mode);
    g_signal_connect(app, "startup",  G_CALLBACK(startup_wizard),  NULL);
    /* Enter main loop */
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (opts & OPT_d)
        delete_dump_dir_possibly_using_abrtd(g_dump_dir_name);

    save_user_settings();

    return 0;
}
