/*
    Copyright (C) 2009, 2010  Red Hat, Inc.

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
#include "internal_libreport.h"
#include "run-command.h"
#include "cli-report.h"
#include "client.h"

/* Field separator for the crash report file that is edited by user. */
#define FIELD_SEP "%----"

/*
 * Escapes the field content string to avoid confusion with file comments.
 * Returned field must be free()d by caller.
 */
static char *escape(const char *str)
{
    // Determine the size of resultant string.
    // Count the required number of escape characters.
    // 1. NEWLINE followed by #
    // 2. NEWLINE followed by \# (escaped version)
    const char *ptr = str;
    bool newline = true;
    int count = 0;
    while (*ptr)
    {
        if (newline)
        {
            if (*ptr == '#')
                ++count;
            if (*ptr == '\\' && ptr[1] == '#')
                ++count;
        }

        newline = (*ptr == '\n');
        ++ptr;
    }

    // Copy the input string to the resultant string, and escape all
    // occurences of \# and #.
    char *result = (char*)xmalloc(strlen(str) + 1 + count);

    const char *src = str;
    char *dest = result;
    newline = true;
    while (*src)
    {
        if (newline)
        {
            if (*src == '#')
                *dest++ = '\\';
            else if (*src == '\\' && *(src + 1) == '#')
                *dest++ = '\\';
        }

        newline = (*src == '\n');
        *dest++ = *src++;
    }
    *dest = '\0';
    return result;
}

/*
 * Removes all comment lines, and unescapes the string previously escaped
 * by escape(). Works in-place.
 */
static void remove_comments_and_unescape(char *str)
{
    char *src = str, *dest = str;
    bool newline = true;
    while (*src)
    {
        if (newline)
        {
            if (*src == '#')
            { // Skip the comment line!
                while (*src && *src != '\n')
                    ++src;

                if (*src == '\0')
                    break;

                ++src;
                continue;
            }
            if (*src == '\\'
                    && (src[1] == '#' || (src[1] == '\\' && src[2] == '#'))
               ) {
                ++src; // Unescape escaped char.
            }
        }

        newline = (*src == '\n');
        *dest++ = *src++;
    }
    *dest = '\0';
}

/*
 * Writes a field of crash report to a file.
 * Field must be writable.
 */
static void write_crash_report_field(FILE *fp, problem_data_t *problem_data,
        const char *field, const char *description)
{
    const struct problem_item *value = problem_data_get_item_or_NULL(problem_data, field);
    if (!value)
    {
        // exit silently, all fields are optional for now
        //error_msg("Field %s not found", field);
        return;
    }

    fprintf(fp, "%s%s\n", FIELD_SEP, field);

    fprintf(fp, "%s\n", description);
    if (!(value->flags & CD_FLAG_ISEDITABLE))
        fprintf(fp, _("# This field is read only\n"));

    char *escaped_content = escape(value->content);
    fprintf(fp, "%s\n", escaped_content);
    free(escaped_content);
}

/*
 * Saves the crash report to a file.
 * Parameter 'fp' must be opened before write_crash_report is called.
 * Returned value:
 *  If the report is successfully stored to the file, a zero value is returned.
 *  On failure, nonzero value is returned.
 */
static void write_crash_report(problem_data_t *report, FILE *fp)
{
    fprintf(fp, "# Please check this report. Lines starting with '#' will be ignored.\n"
            "# Lines starting with '%%----' separate fields, please do not delete them.\n\n");

    write_crash_report_field(fp, report, FILENAME_COMMENT,
            _("# Describe the circumstances of this crash below"));
    write_crash_report_field(fp, report, FILENAME_BACKTRACE,
            _("# Backtrace\n# Check that it does not contain any sensitive data (passwords, etc.)"));
    write_crash_report_field(fp, report, FILENAME_DUPHASH, "# DUPHASH");
    write_crash_report_field(fp, report, FILENAME_ARCHITECTURE, _("# Architecture"));
    write_crash_report_field(fp, report, FILENAME_CMDLINE, _("# Command line"));
    write_crash_report_field(fp, report, FILENAME_COMPONENT, _("# Component"));
    write_crash_report_field(fp, report, FILENAME_COREDUMP, _("# Core dump"));
    write_crash_report_field(fp, report, FILENAME_EXECUTABLE, _("# Executable"));
    write_crash_report_field(fp, report, FILENAME_KERNEL, _("# Kernel version"));
    write_crash_report_field(fp, report, FILENAME_PACKAGE, _("# Package"));
    write_crash_report_field(fp, report, FILENAME_REASON, _("# Reason of crash"));

    if (problem_data_get_item_or_NULL(report, FILENAME_ROOTDIR))
    {
        if (problem_data_get_item_or_NULL(report, FILENAME_OS_INFO_IN_ROOTDIR))
        {
            write_crash_report_field(fp, report, FILENAME_OS_INFO_IN_ROOTDIR,
                    _("# os-release configuration file from root dir"));
        }
        else
        {
            write_crash_report_field(fp, report, FILENAME_OS_RELEASE_IN_ROOTDIR,
                    _("# Release string of the operating system from root dir"));
        }
    }
    else
    {
        if (problem_data_get_item_or_NULL(report, FILENAME_OS_INFO))
        {
            write_crash_report_field(fp, report, FILENAME_OS_INFO,
                    _("# os-release configuration file"));
        }
        else
        {
            write_crash_report_field(fp, report, FILENAME_OS_RELEASE,
                    _("# Release string of the operating system"));
        }
    }
}

/*
 * Updates appropriate field in the report from the text. The text can
 * contain multiple fields.
 * Returns:
 *  0 if no change to the field was detected.
 *  1 if the field was changed.
 *  Changes to read-only fields are ignored.
 */
static int read_crash_report_field(const char *text, problem_data_t *report,
        const char *field)
{
    char separator[sizeof("\n" FIELD_SEP)-1 + strlen(field) + 2]; // 2 = '\n\0'
    snprintf(separator, sizeof(separator), "\n%s%s\n", FIELD_SEP, field);
    const char *textfield = strstr(text, separator);
    if (!textfield)
        return 0; // exit silently because all fields are optional

    textfield += strlen(separator);
    int length = 0;
    const char *end = strstr(textfield, "\n" FIELD_SEP);
    if (!end)
        length = strlen(textfield);
    else
        length = end - textfield;

    struct problem_item *value = problem_data_get_item_or_NULL(report, field);
    if (!value)
    {
        error_msg("Field %s not found", field);
        return 0;
    }

    // Do not change noneditable fields.
    if (!(value->flags & CD_FLAG_ISEDITABLE))
        return 0;

    // Compare the old field contents with the new field contents.
    char newvalue[length + 1];
    strncpy(newvalue, textfield, length);
    newvalue[length] = '\0';
    strtrim(newvalue);

    char oldvalue[strlen(value->content) + 1];
    strcpy(oldvalue, value->content);
    strtrim(oldvalue);

    // Return if no change in the contents detected.
    if (strcmp(newvalue, oldvalue) == 0)
        return 0;

    free(value->content);
    value->content = xstrdup(newvalue);
    return 1;
}

/*
 * Updates the crash report 'report' from the text. The text must not contain
 * any comments.
 * Returns:
 *  0 if no field was changed.
 *  1 if any field was changed.
 *  Changes to read-only fields are ignored.
 */
static int read_crash_report(problem_data_t *report, const char *text)
{
    int result = 0;
    result |= read_crash_report_field(text, report, FILENAME_COMMENT);
    result |= read_crash_report_field(text, report, FILENAME_BACKTRACE);
    result |= read_crash_report_field(text, report, FILENAME_DUPHASH);
    result |= read_crash_report_field(text, report, FILENAME_ARCHITECTURE);
    result |= read_crash_report_field(text, report, FILENAME_CMDLINE);
    result |= read_crash_report_field(text, report, FILENAME_COMPONENT);
    result |= read_crash_report_field(text, report, FILENAME_COREDUMP);
    result |= read_crash_report_field(text, report, FILENAME_EXECUTABLE);
    result |= read_crash_report_field(text, report, FILENAME_KERNEL);
    result |= read_crash_report_field(text, report, FILENAME_PACKAGE);
    result |= read_crash_report_field(text, report, FILENAME_REASON);

    if (problem_data_get_item_or_NULL(report, FILENAME_ROOTDIR))
    {
        if (problem_data_get_item_or_NULL(report, FILENAME_OS_INFO_IN_ROOTDIR))
            result |= read_crash_report_field(text, report, FILENAME_OS_INFO_IN_ROOTDIR);
        else
            result |= read_crash_report_field(text, report, FILENAME_OS_RELEASE_IN_ROOTDIR);
    }
    else
    {
        if (problem_data_get_item_or_NULL(report, FILENAME_OS_INFO))
            result |= read_crash_report_field(text, report, FILENAME_OS_INFO);
        else
            result |= read_crash_report_field(text, report, FILENAME_OS_RELEASE);
    }


    return result;
}

/**
 * Ensures that the fields needed for editor are present in the problem data.
 * Fields: comments.
 */
static void create_fields_for_editor(problem_data_t *problem_data)
{
    if (!problem_data_get_item_or_NULL(problem_data, FILENAME_COMMENT))
        problem_data_add(problem_data, FILENAME_COMMENT, "", CD_FLAG_TXT + CD_FLAG_ISEDITABLE);
}

/**
 * Runs external editor.
 * Returns:
 *  0 if the launch was successful
 *  1 if it failed. The error reason is logged using error_msg()
 */
static int launch_editor(const char *path)
{
    const char *editor, *terminal;

    editor = getenv("ABRT_EDITOR");
    if (!editor)
        editor = getenv("VISUAL");
    if (!editor)
        editor = getenv("EDITOR");

    terminal = getenv("TERM");
    if (!editor && (!terminal || strcmp(terminal, "dumb") == 0))
    {
        error_msg(_("Cannot run vi: $TERM, $VISUAL and $EDITOR are not set"));
        return 1;
    }

    if (!editor)
        editor = "vi";

    char *args[3];
    args[0] = (char*)editor;
    args[1] = (char*)path;
    args[2] = NULL;
    run_command(args);

    return 0;
}

/**
 * Returns:
 *  0 on success, problem data has been updated
 *  2 on failure, unable to create, open, or close temporary file
 *  3 on failure, cannot launch text editor
 */
static int run_report_editor(problem_data_t *problem_data)
{
    /* Open a temporary file and write the crash report to it. */
    char filename[] = LARGE_DATA_TMP_DIR"/abrt-report.XXXXXX";
    int fd = mkstemp(filename);
    if (fd == -1) /* errno is set */
    {
        perror_msg("Can't generate temporary file name");
        return 2;
    }

    FILE *fp = fdopen(fd, "w");
    if (!fp) /* errno is set */
    {
        die_out_of_memory();
    }

    write_crash_report(problem_data, fp);

    if (fclose(fp)) /* errno is set */
    {
        perror_msg("Can't write '%s'", filename);
        return 2;
    }

    // Start a text editor on the temporary file.
    if (launch_editor(filename) != 0)
        return 3; /* exit with error */

    // Read the file back and update the report from the file.
    fp = fopen(filename, "r");
    if (!fp) /* errno is set */
    {
        perror_msg("Can't open '%s' to read the crash report", filename);
        return 2;
    }

    off_t size = fstat_st_size_or_die(fileno(fp));
    if (size > INT_MAX/4)
	    size = INT_MAX/4; /* paranoia */
    char *text = xmalloc(size + 1);
    if (fread(text, 1, size, fp) != size)
    {
        error_msg("Can't read '%s'", filename);
        free(text);
        fclose(fp);
        return 2;
    }
    text[size] = '\0';
    fclose(fp);

    // Delete the tempfile.
    if (unlink(filename) == -1) /* errno is set */
    {
        perror_msg("Can't unlink %s", filename);
    }

    remove_comments_and_unescape(text);
    // Updates the crash report from the file text.
    int report_changed = read_crash_report(problem_data, text);
    free(text);
    if (report_changed)
        puts(_("\nThe report has been updated"));
    else
        puts(_("\nNo changes were detected in the report"));

    return 0;
}

/**
 *  Asks user for missing information
 */
static void ask_for_missing_settings(const char *event_name)
{
    for (int i = 0; i < 3; ++i)
    {
        GList *err_list = NULL, *iter = NULL;
        err_list = get_options_with_err_msg(event_name);
        if(!err_list)
            return;

        event_config_t *event_config = get_event_config(event_name);

        for (iter = err_list; iter; iter = iter->next)
        {
            invalid_option_t *err_data = (invalid_option_t *)iter->data;
            event_option_t *opt = get_event_option_from_list(err_data->invopt_name, event_config->options);

            free(opt->eo_value);
            opt->eo_value = NULL;

            char *question = xasprintf("%s %s:",
                                             ec_get_screen_name(event_config) ? ec_get_screen_name(event_config) : event_name,
                                             (opt->eo_label) ? opt->eo_label : opt->eo_name);
            switch (opt->eo_type) {
            case OPTION_TYPE_TEXT:
            case OPTION_TYPE_NUMBER:
                opt->eo_value = libreport_ask(question);
                break;
            case OPTION_TYPE_PASSWORD:
            {
                opt->eo_value = libreport_ask_password(question);
                break;
            }
            case OPTION_TYPE_BOOL:
                if (libreport_ask_yes_no(question))
                    opt->eo_value = xstrdup("yes");
                else
                    opt->eo_value = xstrdup("no");

                break;
            case OPTION_TYPE_HINT_HTML: /* TODO? */
            case OPTION_TYPE_INVALID:
                break;
            };

            free(question);
        }

        g_list_free_full(err_list, (GDestroyNotify)free_invalid_options);

        err_list = get_options_with_err_msg(event_name);
        if (!err_list)
            return;

        libreport_alert(_("Your input is not valid, because of:"));
        for (iter = err_list; iter; iter = iter -> next)
        {
            invalid_option_t *err_data = (invalid_option_t *)iter->data;
            char *msg = xasprintf(_("Bad value for '%s': %s"),
                                    err_data->invopt_name,
                                    err_data->invopt_error);
            libreport_alert(msg);
            free(msg);
        }

        g_list_free_full(err_list, (GDestroyNotify)free_invalid_options);
    }

    /* we ask for 3 times and still don't have valid information */
    error_msg_and_die("Invalid input, exiting.");
}


/*** Event running ***/

struct logging_state {
    bool output_was_produced;
};

static char *do_log(char *log_line, void *param)
{
    libreport_client_log(log_line);
    return log_line;
}

static char *do_log2(char *log_line, void *param)
{
    struct logging_state *l_state = param;
    l_state->output_was_produced |= (log_line[0] != '\0');
    return do_log(log_line, param);
}

static int export_config_and_run_event(
                struct run_event_state *state,
                const char *dump_dir_name,
                const char *event)
{
    /* Export overridden settings as environment variables */
    GList *env_list = export_event_config(event);

    int r = run_event_on_dir_name(state, dump_dir_name, event);

    unexport_event_config(env_list);

    return r;
}

static int is_not_reportable(problem_data_t *problem_data)
{
    const char *not_reportable = problem_data_get_content_or_NULL(problem_data, FILENAME_NOT_REPORTABLE);
    if (not_reportable)
    {
        printf("%s\n", not_reportable);
        if (get_global_stop_on_not_reportable())
            return 1;
    }
    return 0;
}

static int is_backtrace_rating_usable(event_config_t *config, problem_data_t *problem_data)
{
    char *usability_description = NULL;
    char *usability_detail = NULL;
    const bool usable_rating = check_problem_rating_usability(config, problem_data,
                                                              &usability_description,
                                                              &usability_detail);

    if (!usable_rating)
    {
        printf("%s\n", usability_description);
        printf("%s\n", usability_detail);
    }

    free(usability_description);
    free(usability_detail);

    return usable_rating;
}

static int review_problem_data(const char *dump_dir_name, problem_data_t *problem_data)
{
    /* Open text editor and give a chance to review the backtrace etc */
    create_fields_for_editor(problem_data);
    int result = run_report_editor(problem_data);
    if (result != 0)
        return 1;

    /* Save comment, backtrace */
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (dd)
    {
        //TODO: we should iterate through problem_data and modify all modifiable fields
        const char *comment = problem_data_get_content_or_NULL(problem_data, FILENAME_COMMENT);
        const char *backtrace = problem_data_get_content_or_NULL(problem_data, FILENAME_BACKTRACE);
        if (comment)
            dd_save_text(dd, FILENAME_COMMENT, comment);
        if (backtrace)
            dd_save_text(dd, FILENAME_BACKTRACE, backtrace);
        dd_close(dd);
    }

    return 0;
}

static problem_data_t *load_problem_data_if_not_yet(problem_data_t *problem_data, const char *dump_dir_name)
{
    if (problem_data)
        return problem_data;
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
    {
        return NULL;
    }
    problem_data = create_problem_data_from_dump_dir(dd);
    dd_close(dd);
    return problem_data;
}

static int run_event_on_dir_name_batch(
                struct run_event_state *state,
                const char *dump_dir_name,
                const char *event_name)
{
    int retval = -1;
    problem_data_t *problem_data = NULL;

    event_config_t *config = get_event_config(event_name);
    if (config)
    {
        if (config->ec_minimal_rating != 0)
        {
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            if (!is_backtrace_rating_usable(config, problem_data))
                goto ret;
        }

        if (!config->ec_skip_review)
        {
            /* We assume this event is a reporter
             * (sinse it asks user to review data before it is run)
             */

            /* Is problem non-reportable? */
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            if (is_not_reportable(problem_data))
                goto ret;
        }
    }

    problem_data_free(problem_data);
    problem_data = NULL;

    retval = export_config_and_run_event(state, dump_dir_name, event_name);

 ret:
    problem_data_free(problem_data);
    return retval;
}

static int run_event_on_dir_name_interactively(
                struct run_event_state *state,
                const char *dump_dir_name,
                const char *event_name)
{
    int retval = -1;
    problem_data_t *problem_data = NULL;

    event_config_t *config = get_event_config(event_name);
    if (config)
    {
        if (config->ec_minimal_rating != 0)
        {
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            if (!is_backtrace_rating_usable(config, problem_data))
                goto ret;
        }

        if (!config->ec_skip_review)
        {
            /* We assume this event is a reporter
             * (sinse it asks user to review data before it is run)
             */

            /* Is problem non-reportable? */
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            if (is_not_reportable(problem_data))
                goto ret;
        }

        if (config->ec_sending_sensitive_data)
        {
            char *msg = xasprintf(_("Event '%s' requires permission to send possibly sensitive data."
                                    " Do you want to continue?"),
                        ec_get_screen_name(config) ? ec_get_screen_name(config) : event_name);
            bool ok = libreport_ask_yes_no(msg);
            free(msg);
            if (!ok)
                goto ret;
        }

        /* can't fail */
        ask_for_missing_settings(event_name);

        /* review problem data only if the event needs it */
        if (!config->ec_skip_review)
        {
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            retval = review_problem_data(dump_dir_name, problem_data);
            if (retval != 0)
                /* review failed, an error message was already logged */
                goto ret;
        }
    }

    problem_data_free(problem_data);
    problem_data = NULL;

    retval = export_config_and_run_event(state, dump_dir_name, event_name);

 ret:
    problem_data_free(problem_data);
    return retval;
}

static int choose_number_from_range(unsigned min, unsigned max, const char *message)
{
    unsigned picked;
    unsigned ii;
    for (ii = 0; ii < 3; ++ii)
    {
        char *answer = libreport_ask(message);

        picked = xatou(answer);
        if (min <= picked && picked <= max)
            return picked;

        char *msg = xasprintf("%s (%u - %u)\n", _("You have chosen number out of range"), min, max);
        libreport_alert(msg);
        free(msg);
    }

    error_msg_and_die(_("Invalid input, exiting."));
}

static char *select_event_name(GList *list_options)
{
    if (!list_options)
        return NULL;

    /* Just one? */
    if (!list_options->next)
        return xstrdup((char*)list_options->data);

    int count = 0;
    for (GList *li = list_options; li; li = li->next)
    {
        char *opt = (char*)li->data;
        event_config_t *config = get_event_config(opt);
        count++;
        printf("%2i) %s\n", count, config ? ec_get_screen_name(config) : opt);
    }

    const unsigned picked = choose_number_from_range(1, count, _("Select an event to run: "));
    GList *chosen = g_list_nth(list_options, picked - 1);
    return xstrdup((char*)chosen->data);
}

int select_and_run_one_event(const char *dump_dir_name, const char *pfx, int interactive)
{
    GList *list_events = list_possible_events_glist(dump_dir_name, pfx);
    char *event_name = select_event_name(list_events);
    list_free_with_free(list_events);

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log;
    int r = interactive
                ? run_event_on_dir_name_interactively(run_state, dump_dir_name, event_name)
                : run_event_on_dir_name_batch(run_state, dump_dir_name, event_name)
                ;

    free_run_event_state(run_state);

    free(event_name);

    return r;
}

static int run_event_chain_real(struct run_event_state *run_state,
                                const char             *dump_dir_name,
                                GList                  *chain,
                                int                     interactive)
{
    struct logging_state l_state;
    int retval = 0;

    /* Blergh. */
    run_state->logging_callback = do_log2;
    run_state->logging_param = &l_state;

    for (GList *eitem = chain; eitem; eitem = g_list_next(eitem))
    {
        l_state.output_was_produced = false;
        const char *event_name = eitem->data;
        log_info("running commands for event '%s'", event_name);

        if (interactive)
            retval = run_event_on_dir_name_interactively(run_state, dump_dir_name,
			    event_name);
        else
            retval = run_event_on_dir_name_batch(run_state, dump_dir_name,
			    event_name);

        if (retval < 0)
            /* Nothing was run (bad backtrace, user declined, etc.) */
            break;
        if (retval == 0 && run_state->children_count == 0)
        {
            /* Continue running the chain even if no actions were executed for
             * this event.
             */
            log_warning("no actions matching this problem found for event '%s'",
                    event_name);
            continue;
        }
        else if (retval != 0 || !l_state.output_was_produced)
        {
            /* Program failed, or finished successfully with no output. */
            char *msg = exit_status_as_string(event_name, run_state->process_status);
            fputs(msg, stdout);
            free(msg);
        }
        if (retval != 0)
            break;
    }

    return retval;
}

/*
 * Run events from a chain. Perform the following steps for each event:
 * 1. Terminate the chain run if the backtrace is not usable.
 * 2. Ask for missing settings.
 * 3. Perform review of problem data if the event requires review.
 * 4. Run event's commands.
 * 5. Terminate the run if any event from the chain requires termination
 *    (i.e. if it exited with EXIT_STOP_EVENT_RUN).
 * 6. Terminate the run if any error occurred.
 * 7. Continue processing next event.
 */
int run_event_chain(const char *dump_dir_name, GList *chain, int interactive)
{
    /* Expand *-wildcards in the names of events in the chain. */
    GList *expanded_chain = expand_event_chain_wildcards(chain);
    struct run_event_state *run_state = new_run_event_state();

    int retval = run_event_chain_real(run_state, dump_dir_name, expanded_chain, interactive);

    free_run_event_state(run_state);
    g_list_free_full(expanded_chain, free);

    return retval;
}

static workflow_t *select_workflow(GHashTable *workflows)
{
    GList *wf_list = g_hash_table_get_values(workflows);

    if (wf_list == NULL)
    {
        error_msg("No workflow suitable for this problem was found!");
        return NULL;
    }

    const guint wf_cnt = g_list_length(wf_list);
    if (wf_cnt == 1)
    {
        workflow_t *wf_selected = (workflow_t *)wf_list->data;
        log_info("autoselected workflow: '%s'", (char *)wf_get_name(wf_selected));
        g_list_free(wf_list);
        return wf_selected;
    }

    wf_list = g_list_sort(wf_list, (GCompareFunc)wf_priority_compare);

    workflow_t *help_wf_array[wf_cnt];
    unsigned count = 0;

    for(GList *wf_iter = wf_list; wf_iter; wf_iter = g_list_next(wf_iter))
    {
        workflow_t *wf = (workflow_t *)wf_iter->data;
        help_wf_array[count] = wf;
        printf("%d %s\n  %s\n\n", ++count, wf_get_screen_name(wf), wf_get_description(wf));
    }

    g_list_free(wf_list);

    const unsigned picked = choose_number_from_range(1, count, _("Select a workflow to run: "));
    return help_wf_array[picked - 1];
}

int select_and_run_workflow(const char *dump_dir_name, GHashTable *workflows, int interactive)
{
    workflow_t *workflow;
    GList *events;
    const char *workflow_name;
    char *environment_variable;
    struct run_event_state *run_state;
    int retval;

    workflow = select_workflow(workflows);
    if (NULL == workflow)
    {
        return -1;
    }

    events = wf_get_event_names(workflow);

    workflow_name = wf_get_name(workflow);
    environment_variable = g_strdup_printf("LIBREPORT_WORKFLOW=%s", workflow_name);

    run_state = new_run_event_state();
    g_ptr_array_add(run_state->extra_environment, environment_variable);

    retval = run_event_chain_real(run_state, dump_dir_name, events, interactive);
    free_run_event_state(run_state);

    return retval;
}
