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


void reload_problem_data_from_dump_dir(void)
{
    free(g_events);

    struct dump_dir *dd = dd_opendir(g_dump_dir_name, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die(); /* dd_opendir already logged error msg */

    problem_data_t *new_cd = create_problem_data_from_dump_dir(dd);
    add_to_problem_data_ext(new_cd, CD_DUMPDIR, g_dump_dir_name, (CD_FLAG_TXT | CD_FLAG_ISNOTEDITABLE));

    g_events = list_possible_events(dd, NULL, "");
    dd_close(dd);

    if (1)
    {
        /* Copy "selected for reporting" flags */
        GHashTableIter iter;
        char *name;
        struct problem_item *new_item;
        g_hash_table_iter_init(&iter, new_cd);
        while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&new_item))
        {
            struct problem_item *old_item = g_cd ? get_problem_data_item_or_NULL(g_cd, name) : NULL;
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
        free_problem_data(g_cd);
    }
    g_cd = new_cd;

}

int main(int argc, char **argv)
{
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
        "& [-vpd] [-g GUI_FILE] [-n PROG_NAME] DIR\n"
        "\n"
        "GUI tool to analyze and report problem saved in specified DIR"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_g = 1 << 1,
        OPT_p = 1 << 2,
        OPT_d = 1 << 3,
        OPT_e = 1 << 4,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_STRING('g', NULL, &g_glade_file, "FILE",          _("Alternate GUI file")),
        OPT_BOOL(  'p', NULL, NULL,                           _("Add program names to log")),
        /* for use from 3rd party apps to show just a reporter selector */
        OPT_BOOL(  'd', "delete", NULL,                       _("Remove DIR after reporting")),
        OPT_LIST(  'e', "event", &g_auto_event_list, "EVENT", _("Run only this event")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;
    if (!argv[0] || argv[1]) /* zero or >1 arguments */
        show_usage_and_die(program_usage_string, program_options);

    export_abrt_envvars(opts & OPT_p);

    g_dump_dir_name = xstrdup(argv[0]);

    /* load /etc/abrt/events/foo.{conf,xml} stuff
       and $XDG_CACHE_HOME/abrt/events/foo.conf */
    load_event_config_data();
    load_event_config_data_from_keyring();
    load_user_settings("report-gtk");

    reload_problem_data_from_dump_dir();

    create_assistant();

    g_custom_logger = &show_error_as_msgbox;

    update_gui_state_from_problem_data();

    /* Enter main loop */
    gtk_main();

    if (opts & OPT_d)
        delete_dump_dir_possibly_using_abrtd(g_dump_dir_name);

    save_user_settings();

    return 0;
}
