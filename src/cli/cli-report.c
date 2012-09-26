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
    write_crash_report_field(fp, report, FILENAME_OS_RELEASE, _("# Release string of the operating system"));
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
    result |= read_crash_report_field(text, report, FILENAME_OS_RELEASE);
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
    char filename[] = "/tmp/abrt-report.XXXXXX";
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

    fseek(fp, 0, SEEK_END);
    unsigned long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *text = (char*)xmalloc(size + 1);
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
 * Asks user for a text response.
 * @param question
 *  Question displayed to user.
 * @param result
 *  Output array.
 * @param result_size
 *  Maximum byte count to be written.
 */
static void read_from_stdin(const char *question, char *result, int result_size)
{
    assert(result_size > 1);
    printf("%s", question);
    fflush(NULL);
    if (NULL == fgets(result, result_size, stdin))
        result[0] = '\0';
    // Remove the newline from the login.
    strchrnul(result, '\n')[0] = '\0';
}

/**
 * Asks a [y/n] question on stdin/stdout.
 * Returns true if the answer is yes, false otherwise.
 */
static bool ask_yesno(const char *question)
{
    /* The response might take more than 1 char in non-latin scripts. */
    const char *yes = _("y");
    const char *no = _("N");
    printf("%s [%s/%s]: ", question, yes, no);
    fflush(NULL);

    char answer[16];
    if (!fgets(answer, sizeof(answer), stdin))
        return false;
    /* Use strncmp here because the answer might contain a newline as
       the last char. */
    return 0 == strncmp(answer, yes, strlen(yes));
}

/* Returns true if the string contains the specified number. */
static bool is_number_in_string(unsigned number, const char *str)
{
    const char *c;
    char numstr[sizeof(int) * 3 + 2];
    int len;

    len = snprintf(numstr, sizeof(numstr), "%u", number);
    for (c = str; *c; c++)
    {
        c = strstr(c, numstr);
        if (!c)
            /* no such number exists in the string */
            return false;
        if ((c == str || !isalnum(c[-1])) && !isalnum(c[len]))
            /* found */
            return true;

        /* found, but it's part of another number. Continue
         * from the next position. */
    }

    return false;
}

/**
 *  Asks user for missing information
 */
static void ask_for_missing_settings(const char *event_name)
{
    for (int i = 0; i < 3; ++i)
    {
        GHashTable *error_table = validate_event(event_name);
        if (!error_table)
            return;

        event_config_t *event_config = get_event_config(event_name);

        GHashTableIter iter;
        char *opt_name, *err_msg;
        g_hash_table_iter_init(&iter, error_table);
        while (g_hash_table_iter_next(&iter, (void**)&opt_name, (void**)&err_msg))
        {
            event_option_t *opt = get_event_option_from_list(opt_name,
                                                             event_config->options);

            free(opt->eo_value);
            opt->eo_value = NULL;

            char result[512];

            char *question = xasprintf("%s: ", (opt->eo_label) ? opt->eo_label : opt->eo_name);
            switch (opt->eo_type) {
            case OPTION_TYPE_TEXT:
            case OPTION_TYPE_NUMBER:
                read_from_stdin(question, result, 512);
                opt->eo_value = xstrdup(result);
                break;
            case OPTION_TYPE_PASSWORD:
            {
                bool changed = set_echo(false);
                read_from_stdin(question, result, 512);
                if (changed)
                    set_echo(true);

                opt->eo_value = xstrdup(result);
                /* Newline was not added by pressing Enter because ECHO was
                   disabled, so add it now. */
                puts("");
                break;
            }
            case OPTION_TYPE_BOOL:
                if (ask_yesno(question))
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

        g_hash_table_destroy(error_table);

        error_table = validate_event(event_name);
        if (!error_table)
            return;

        log(_("Your input is not valid, because of:"));
        g_hash_table_iter_init(&iter, error_table);
        while (g_hash_table_iter_next(&iter, (void**)&opt_name, (void**)&err_msg))
            log(_("Bad value for '%s': %s"), opt_name, err_msg);

        g_hash_table_destroy(error_table);
    }

    /* we ask for 3 times and still don't have valid infromation */
    error_msg_and_die("Invalid input, program exiting...");
}

struct logging_state {
    char *last_line;
};
static char *do_log_and_save_line(char *log_line, void *param)
{
    struct logging_state *l_state = (struct logging_state *)param;
    log("%s", log_line);
    free(l_state->last_line);
    l_state->last_line = log_line;
    return NULL;
}

static int run_event(struct run_event_state *state, const char *dump_dir_name,
                     const char *event, struct logging_state *l_state)
{
    // Export overridden settings as environment variables
    GList *env_list = export_event_config(event);

    int r = run_event_on_dir_name(state, dump_dir_name, event);
    if (r == 0 && state->children_count == 0)
    {
        l_state->last_line = xasprintf("Error: no processing is specified for event '%s'",
                event);
        r = -1;
    }

    // Unexport overridden settings
    unexport_event_config(env_list);

    return r;
}

static int run_events(const char *dump_dir_name, GList *events, const char *event_type)
{
    int error_cnt = 0;

    // Run events
    struct logging_state l_state;
    l_state.last_line = NULL;
    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log_and_save_line;
    run_state->logging_param = &l_state;
    for (GList *li = events; li; li = li->next)
    {
        char *event = (char *) li->data;
        if (run_event(run_state, dump_dir_name, event, &l_state) == 0)
        {
            printf("%s: ", event);
            if (l_state.last_line)
                printf("%s\n", l_state.last_line);
            else
                printf("%s succeeded\n", event_type);
        }
        else
        {
            error_msg("%s via '%s' was not successful%s%s",
                    event_type,
                    event,
                    l_state.last_line ? ": " : "",
                    l_state.last_line ? l_state.last_line : ""
                    );
            ++error_cnt;
        }
        free(l_state.last_line);
        l_state.last_line = NULL;
    }
    free_run_event_state(run_state);

    return error_cnt;
}

static char *do_log(char *log_line, void *param)
{
    log("%s", log_line);
    return log_line;
}

int run_analyze_event(const char *dump_dir_name, const char *analyzer)
{
    VERB2 log("run_analyze_event('%s')", dump_dir_name);

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log;
    int res = run_event_on_dir_name(run_state, dump_dir_name, analyzer);
    free_run_event_state(run_state);
    return res;
}

/* show even description? */
char *select_event_option(GList *list_options)
{
    if (!list_options)
        return NULL;

    unsigned count = g_list_length(list_options);
    if (count == 1)
        return xstrdup((char*)list_options->data);

    int pos = 0;
    fprintf(stdout, _("How you would like to analyze the problem?\n"));
    for (GList *li = list_options; li; li = li->next)
    {
        char *opt = (char*)li->data;
        event_config_t *config = get_event_config(opt);
        if (config)
        {
            ++pos;
            printf(" %i) %s\n", pos, config->screen_name);
        }
    }

    unsigned picked;
    unsigned ii;
    for (ii = 0; ii < 3; ++ii)
    {
        char answer[16];

        read_from_stdin(_("Select analyzer: "), answer, sizeof(answer));
        if (!*answer)
            continue;

        picked = xatou(answer);
        if (picked > count || picked < 1)
        {
            fprintf(stdout, _("You have chosen number out of range"));
            fprintf(stdout, "\n");
            continue;
        }

        break;
    }

    if (ii == 3)
        error_msg_and_die(_("Invalid input, program exiting..."));

    GList *choosen = g_list_nth(list_options, picked - 1);
    return xstrdup((char*)choosen->data);
}

GList *str_to_glist(char *str, int delim)
{
    GList *list = NULL;
    while (*str)
    {
        char *end = strchrnul(str, delim);
        if (end != str)
            list = g_list_append(list, xstrndup(str, end - str));

        str = end;
        if (!*str)
            break;
        str++;
    }

    if (!list && !g_list_length(list))
        return NULL;

    return list;
}

/* Runs collect_* events if there are any. If batch is nonzero, no questions
 * are asked and positive answers are assumed, i.e. all events are ran.
 * returns:  0: success
 *          -1: failed to open dumpdir
 *          >0: number of errors encountered when running the events
 */
int collect(const char *dump_dir_name, int batch)
{
    int errors = 0;
    char wanted_collectors[255];
    GList *selected_events = NULL;

    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
    if (!dd)
        return -1;

    char *collect_events_as_lines = list_possible_events(dd, NULL, "collect");
    dd_close(dd);

    /* return if there are no collect events */
    if (!collect_events_as_lines)
        return 0;

    if (!*collect_events_as_lines)
    {
        free(collect_events_as_lines);
        return 0;
    }

    GList *list_collect_events = str_to_glist(collect_events_as_lines, '\n');
    free(collect_events_as_lines);

    if (!batch)
    {
        GList *li;
        unsigned i;

        puts(_("What additional information would you like to collect?"));
        /* Print list of collectors and ask the user which should be used. */
        for (li = list_collect_events, i = 1; li; li = li->next, i++)
        {
            char *collector_name = (char *) li->data;
            event_config_t *config = get_event_config(collector_name);

            if (!config)
                VERB1 log("No configuration file found for collector '%s'", collector_name);

            printf(" %d) %s\n", i, (config && config->screen_name) ? config->screen_name : collector_name);
        }

        read_from_stdin(_("Select collector(s): "), wanted_collectors, sizeof(wanted_collectors));

        for (li = list_collect_events, i = 1; li; li = li->next, i++)
        {
            char *collector_name = (char *) li->data;

            /* Was this collector requested? */
            if (!is_number_in_string(i, wanted_collectors))
                continue;

            selected_events = g_list_append(selected_events, collector_name);
        }
    }
    else
    {
        /* run all collectors in noninteractive mode */
        selected_events = list_collect_events;
    }

    errors = run_events(dump_dir_name, selected_events, "Collection");

    list_free_with_free(list_collect_events);
    if (!batch)
        g_list_free(selected_events);

    return errors;
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

/* Report the crash */
int report(const char *dump_dir_name, int flags)
{
    /* Load problem_data from (possibly updated by analyze) dump dir */
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        return -1;

    char *not_reportable = dd_load_text_ext(dd, FILENAME_NOT_REPORTABLE, 0
                                            | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE
                                            | DD_FAIL_QUIETLY_ENOENT
                                            | DD_FAIL_QUIETLY_EACCES);

    if (not_reportable)
    {
        char *reason = dd_load_text_ext(dd, FILENAME_REASON, 0
                                        | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);
        char *t = xasprintf("%s%s%s",
                            not_reportable ?: "",
                            not_reportable ? ": " : "",
                            reason ?: _("(no description)"));

        dd_close(dd);
        error_msg("%s", t);
        free(t);
        xfunc_die();
    }

    char *analyze_events_as_lines = list_possible_events(dd, NULL, "analyze");
    dd_close(dd);

    if (analyze_events_as_lines && *analyze_events_as_lines)
    {
        GList *list_analyze_events = str_to_glist(analyze_events_as_lines, '\n');
        free(analyze_events_as_lines);

        char *event = select_event_option(list_analyze_events);
        list_free_with_free(list_analyze_events);

        int analyzer_result = run_analyze_event(dump_dir_name, event);
        free(event);

        if (analyzer_result != 0)
            return 1;
    }

    /* Run collect events if there are any */
    int collect_res = collect(dump_dir_name, flags & CLI_REPORT_BATCH);
    if (collect_res == -1)
        return -1;
    else if (collect_res > 0)
        printf(_("There were %d errors while collecting additional data\n"), collect_res);

    /* Load dd from (possibly updated by collect) dump dir */
    dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        return -1;

    char *report_events_as_lines = list_possible_events(dd, NULL, "report");
    problem_data_t *problem_data = create_problem_data_from_dump_dir(dd);
    dd_close(dd);

    if (!(flags & (CLI_REPORT_BATCH))
        && review_problem_data(dump_dir_name, problem_data))
    {
        problem_data_free(problem_data);
        free(report_events_as_lines);
        return 1;
    }

    /* Get possible reporters associated with this particular crash */
    GList *report_events = NULL;
    if (report_events_as_lines && *report_events_as_lines)
        report_events = str_to_glist(report_events_as_lines, '\n');

    free(report_events_as_lines);

    if (!report_events)
    {
        problem_data_free(problem_data);
        error_msg_and_die("The problem directory '%s' has no defined reporters",
                          dump_dir_name);
    }

    /* Get settings */
    load_event_config_data();

    int errors = 0;
    int plugins = 0;
    if (flags & CLI_REPORT_BATCH)
    {
        puts(_("Reporting..."));
        errors += run_events(dump_dir_name, report_events, "Reporting");
        plugins += g_list_length(report_events);
    }
    else
    {
        unsigned i;
        GList *li;
        char wanted_reporters[255];

        puts(_("How would you like to report the problem?"));
        /* Print list of reporters and ask the user which should be used. */
        for (li = report_events, i = 1; li; li = li->next, i++)
        {
            char *reporter_name = (char *) li->data;
            event_config_t *config = get_event_config(reporter_name);

            printf(" %d) %s\n", i, (config && config->screen_name) ? config->screen_name : reporter_name);
        }

        read_from_stdin(_("Select reporter(s): "), wanted_reporters, sizeof(wanted_reporters));

        for (li = report_events, i = 1; li; li = li->next, i++)
        {
            char *reporter_name = (char *) li->data;
            event_config_t *config = get_event_config(reporter_name);

            if (!config)
                VERB1 log("No configuration file found for '%s' reporter", reporter_name);

            /* Was this reporter requested? */
            if (!is_number_in_string(i, wanted_reporters))
                continue;

            if (!is_backtrace_rating_usable(config, problem_data))
            {
                errors++;
                goto next_plugin;
            }

            ask_for_missing_settings(reporter_name);

            /*
             * to avoid creating list with one item, we probably should
             * provide something like
             * run_event(char*, char*)
             */
            GList *cur_event = NULL;
            cur_event = g_list_append(cur_event, reporter_name);
            errors += run_events(dump_dir_name, cur_event, "Reporting");
            g_list_free(cur_event);

next_plugin:
            plugins++;
        }
    }

    printf(_("Problem reported via %d report events (%d errors)\n"), plugins, errors);
    problem_data_free(problem_data);
    list_free_with_free(report_events);
    return errors;
}

struct chain_logging_param
{
    struct logging_state l_state;
    bool terminate;
};

static char *do_log_and_terminate_chain_on_request(char *log_line, void *param)
{
    struct chain_logging_param *arg = (struct chain_logging_param *)param;

    arg->terminate = strcmp("THANKYOU", log_line) == 0;

    return do_log_and_save_line(log_line, &(arg->l_state));
}

/*
 * Runs event from chain. Perform the following steps for each event from chain.
 * 1. Terminates a chain run if a backtrace is not usable.
 * 2. Asks for missing settings.
 * 3. Performs review of problem data if event requires review.
 * 4. Runs events commands.
 * 5. Terminates a chain run if any event from the chain requires termination
 *    (prints THANKYOU to stdout in the current implementation)
 * 6. Terminates a chain run if any error occurs.
 * 7. Continues immediately with processing of next event.
 */
int run_events_chain(const char *dump_dir_name, GList *chain)
{
    struct dump_dir *dd = dd_opendir(dump_dir_name, /*flags:*/ 0);
    if (!dd)
        return -1;

    problem_data_t *problem_data = create_problem_data_from_dump_dir(dd);

    dd_close(dd);

    struct chain_logging_param chain_state;
    memset(&chain_state, 0, sizeof(chain_state));

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log_and_terminate_chain_on_request;
    run_state->logging_param = &chain_state;

    int returncode = 0;

    for (GList *eitem = chain; !chain_state.terminate && eitem; eitem = g_list_next(eitem))
    {
        const char *const event = (const char *)eitem->data;
        event_config_t *config = get_event_config(event);

        if (!is_backtrace_rating_usable(config, problem_data))
            /* it is not a failure of event if the backtrace is unusable */
            break;

        if (config)
        {
            if (config->ec_sending_sensitive_data)
            {
                char *msg = xasprintf(_("Event '%s' requires permission to send possibly sensitive data."
                                        " Do you want to continue?"),
                            config->screen_name ? config->screen_name : event);
                const bool response = ask_yesno(msg);
                free(msg);
                if (!response)
                    break;
            }

            /* can't fail */
            ask_for_missing_settings(event);

            /* review problem data only if the event needs it */
            if (!config->ec_skip_review
                && review_problem_data(dump_dir_name, problem_data))
            {
                /* review failed, an error message was already logged */
                returncode = -1;
                break;
            }
        }

        returncode = run_event(run_state, dump_dir_name, event, &chain_state.l_state);
        if (returncode != 0)
        {
            error_msg("'%s' event was not successful%s%s",
                      event,
                      chain_state.l_state.last_line ? ": " : "",
                      chain_state.l_state.last_line ? chain_state.l_state.last_line : ""
                     );
            break;
        }

        free(chain_state.l_state.last_line);
        chain_state.l_state.last_line = NULL;
    }

    free_run_event_state(run_state);
    free(chain_state.l_state.last_line);
    problem_data_free(problem_data);

    return returncode;
}
