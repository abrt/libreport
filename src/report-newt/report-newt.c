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

#include <newt.h>
#include "internal_libreport.h"
#if HAVE_LOCALE_H
# include <locale.h>
#endif

struct reporter {
    char *name;
    event_config_t *config;
    bool selected;
};

static GArray *get_available_reporters(char *events)
{
    GArray *reporters = g_array_new(FALSE, FALSE, sizeof (struct reporter));;
    struct reporter r;
    char *s;

    for (s = events; (events = strtok(s, "\n")); s = NULL)
    {
        r.name = events;
        r.config = get_event_config(events);
        r.selected = 0;
        g_array_append_val(reporters, r);
    }

    return reporters;
}

static int select_reporters(GArray *reporters)
{
    newtGrid grid, cgrid, bgrid;
    newtComponent *checkboxes, text, form, button_ok, button_cancel;
    int i, selected;

    text = newtTextboxReflowed(0, 0, _("How would you like to report the problem?"), 35, 5, 5, 0);

    checkboxes = g_alloca(sizeof (newtComponent) * reporters->len);
    cgrid = newtCreateGrid(1, reporters->len);
    for (i = 0; i < reporters->len; i++)
    {
        struct reporter *r = &g_array_index(reporters, struct reporter, i);

        checkboxes[i] = newtCheckbox(20, i + 1, r->config && r->config->screen_name ?
                r->config->screen_name : r->name, 0, NULL, NULL);
        newtGridSetField(cgrid, 0, i, NEWT_GRID_COMPONENT, checkboxes[i], 0, 0, 0, 0, NEWT_ANCHOR_LEFT, 0);
    }

    bgrid = newtButtonBar(_("Ok"), &button_ok, _("Cancel"), &button_cancel, NULL);

    grid = newtGridBasicWindow(text, cgrid, bgrid);
    newtGridWrappedWindow(grid, NULL);

    form = newtForm (NULL, NULL, 0);
    newtGridAddComponentsToForm(grid, form, 1);

    selected = 0;
    if (newtRunForm(form) == button_ok)
    {
        for (i = 0; i < reporters->len; i++)
            if (newtCheckboxGetValue(checkboxes[i]) == '*')
            {
                g_array_index(reporters, struct reporter, i).selected = 1;
                selected++;
            }
    }

    newtFormDestroy(form);
    newtGridFree(grid, 1);
    newtPopWindow();

    return selected;
}

static int configure_reporter(struct reporter *r, bool skip_if_valid)
{
    GHashTable *error_table;
    GList *option;
    event_option_t *opt;
    bool first = true, cancel = false;
    int num_opts, i;
    newtComponent text, *options, button_ok, button_cancel, form;
    newtGrid grid, ogrid, bgrid;

    while ((error_table = validate_event(r->name)) ||
            (!skip_if_valid && first && r->config))
    {
        text = newtTextboxReflowed(0, 0, r->config->screen_name ?
                r->config->screen_name : r->name, 35, 5, 5, 0);

        num_opts = g_list_length(r->config->options);
        options = xmalloc(sizeof (newtComponent) * num_opts);
        ogrid = newtCreateGrid(2, num_opts);

        for (option = r->config->options, i = 0; option && i < num_opts;
                option = g_list_next(option), i++)
        {
            opt = (event_option_t *)option->data;
            switch (opt->eo_type)
            {
                case OPTION_TYPE_TEXT:
                case OPTION_TYPE_NUMBER:
                    options[i] = newtEntry(0, 0, opt->eo_value, 30, NULL,
                            NEWT_FLAG_SCROLL);
                    break;
                case OPTION_TYPE_PASSWORD:
                    options[i] = newtEntry(0, 0, opt->eo_value, 30, NULL,
                            NEWT_FLAG_SCROLL | NEWT_FLAG_PASSWORD);
                    break;
                case OPTION_TYPE_BOOL:
                    options[i] = newtCheckbox(0, 0, "", opt->eo_value &&
                            !strcmp(opt->eo_value, "yes") ? '*' : ' ', NULL, NULL);
                    break;
                default: /* TODO? */
                    options[i] = NULL;
                    break;
            }

            newtGridSetField(ogrid, 0, i, NEWT_GRID_COMPONENT,
                    newtLabel(0, 0, opt->eo_label ? opt->eo_label : opt->eo_name),
                    0, 0, 1, 0, NEWT_ANCHOR_LEFT, 0);
            if (options[i])
                newtGridSetField(ogrid, 1, i, NEWT_GRID_COMPONENT, options[i], 0, 0, 0, 0, NEWT_ANCHOR_LEFT, 0);
        }
        assert(i == num_opts);

        bgrid = newtButtonBar(_("Ok"), &button_ok, _("Cancel"), &button_cancel, NULL);

        grid = newtGridBasicWindow(text, ogrid, bgrid);
        newtGridWrappedWindow(grid, NULL);

        form = newtForm(NULL, NULL, 0);
        newtGridAddComponentsToForm(grid, form, 1);

        if (!first && error_table)
        {
            GHashTableIter iter;
            char *opt_name, *err_msg, buf[4096];

            /* Catenate the error messages */
            buf[0] = '\0';
            for (g_hash_table_iter_init(&iter, error_table);
                    g_hash_table_iter_next(&iter, (void**)&opt_name, (void**)&err_msg); )
            {
                opt = get_event_option_from_list(opt_name, r->config->options);
                snprintf(buf + strlen(buf), sizeof (buf) - strlen(buf), "%s: %s\n",
                        opt->eo_label ? opt->eo_label : opt->eo_name, err_msg);
            }

            newtWinMessage(_("Error"), _("Ok"), buf);
        }

        if (newtRunForm(form) == button_ok)
        {
            for (option = r->config->options, i = 0; option && i < num_opts;
                    option = g_list_next(option), i++)
            {
                opt = (event_option_t *)option->data;
                switch (opt->eo_type)
                {
                    case OPTION_TYPE_TEXT:
                    case OPTION_TYPE_NUMBER:
                    case OPTION_TYPE_PASSWORD:
                        free(opt->eo_value);
                        opt->eo_value = strdup(newtEntryGetValue(options[i]));
                        break;
                    case OPTION_TYPE_BOOL:
                        free(opt->eo_value);
                        opt->eo_value = strdup(newtCheckboxGetValue(options[i]) == '*' ? "yes" : "no");
                        break;
                    default:
                        break;
                }
            }
        }
        else
            cancel = true;

        newtFormDestroy(form);
        newtGridFree(grid, 1);
        newtPopWindow();

        free(options);

        if (error_table)
            g_hash_table_destroy(error_table);
        if (cancel)
            break;
        first = false;
    }

    return !error_table;
}

struct log {
    newtComponent co;
    char *text;
};

static char *save_log_line(char *log_line, void *param)
{
    struct log *log = (struct log *)param;
    char *new;
    size_t len;

    if (log->text == NULL)
    {
        log->text = log_line;
        newtTextboxSetText(log->co, log_line);
    }
    else
    {
        /* Append the log line */
        len = strlen(log->text) + 1 + strlen(log_line) + 1;
        new = xmalloc(len);
        snprintf(new, len, "%s\n%s", log->text, log_line);
        free(log->text);
        free(log_line);
        log->text = new;
        newtTextboxSetText(log->co, new);
    }

    newtRefresh();

    return NULL;
}

static void run_reporter(const char *dump_dir_name, struct reporter *r)
{
    newtComponent text, form, button;
    newtGrid grid, bgrid;
    GList *env_list;
    struct run_event_state *run_state;
    struct log log;
    int x;

    text = newtTextboxReflowed(0, 0, _("Reporting"), 35, 5, 5, 0);
    log.co = newtTextbox(0, 0, 60, 20, NEWT_FLAG_WRAP | NEWT_FLAG_SCROLL);
    log.text = NULL;
    bgrid = newtButtonBar(_("Ok"), &button, NULL);
    grid = newtGridSimpleWindow(text, log.co, bgrid);

    newtGridWrappedWindow(grid, NULL);

    form = newtForm (NULL, NULL, NEWT_FLAG_SCROLL);
    newtFormAddComponents(form, text, log.co, button, NULL);

    newtDrawForm(form);

    run_state = new_run_event_state();
    run_state->logging_callback = save_log_line;
    run_state->logging_param = &log;

    /* Export overridden settings as environment variables */
    env_list = export_event_config(r->name);

    save_log_line(xasprintf(_("--- Running %s ---"), r->name), &log);

    x = run_event_on_dir_name(run_state, dump_dir_name, r->name);
    if (x)
        save_log_line(xasprintf("(exited with %d)", x), &log);

    newtFormSetCurrent(form, button);
    newtRunForm(form);

    unexport_event_config(env_list);
    free_run_event_state(run_state);
    free(log.text);

    newtFormDestroy(form);
    newtGridFree(grid, 1);
    newtPopWindow();
}

static void run_selected_reporters(const char *dump_dir_name, GArray *reporters)
{
    int i;

    for (i = 0; i < reporters->len; i++)
    {
        struct reporter *r = &g_array_index(reporters, struct reporter, i);

        if (!r->selected)
            continue;

        if (!configure_reporter(r, true))
            continue;

        run_reporter(dump_dir_name, r);
    }
}

static int report(const char *dump_dir_name)
{
    GArray *reporters;
    struct dump_dir *dd;
    char *events_as_lines;

    if (!(dd = dd_opendir(dump_dir_name, 0)))
        return -1;
    events_as_lines = list_possible_events(dd, NULL, "report");
    dd_close(dd);

    reporters = get_available_reporters(events_as_lines);

    if (reporters->len > 0)
    {
        if (select_reporters(reporters) > 0)
            run_selected_reporters(dump_dir_name, reporters);
    }
    else
        newtWinMessage(NULL, _("Ok"), _("No reporters available"));

    g_array_free(reporters, TRUE);
    free(events_as_lines);

    return 0;
}

int main(int argc, char **argv)
{
    char *dump_dir_name = NULL;

    abrt_init(argv);

    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif

    /* Can't keep these strings/structs static: _() doesn't support that */
    const char *program_usage_string = _(
        "\b [-od] DIR\n"
        "\n"
        "newt tool to report problem saved in specified DIR"
    );
    enum {
        OPT_o = 1 << 0, // report only
        OPT_r = 1 << 1,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT_BOOL('o', "report-only", NULL, _("Skip analyze steps, go through report steps only")),
        OPT_BOOL('d', "delete", NULL,      _("Remove DIR after reporting")),
        OPT_END()
    };
    unsigned opts = parse_opts(argc, argv, program_options, program_usage_string);
    argv += optind;
    if (!argv[0] || argv[1] || !(opts & OPT_o)) /* zero or >1 arguments */
        show_usage_and_die(program_usage_string, program_options);

    dump_dir_name = argv[0];

    /* Get settings */
    load_event_config_data();

    newtInit();
    newtCls();

    report(dump_dir_name);

    if (opts & OPT_r)
        delete_dump_dir_possibly_using_abrtd(dump_dir_name);

    newtFinished();

    free_event_config_data();

    return 0;
}
