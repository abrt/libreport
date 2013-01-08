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
    // Remove the trailing newline
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
    error_msg_and_die("Invalid input, exiting.");
}


/*** Event running ***/

struct logging_state {
    bool saw_THANKYOU;
    bool output_was_produced;
};

static char *do_log(char *log_line, void *param)
{
    log("%s", log_line);
    return log_line;
}

static char *do_log_and_check_for_THANKYOU(char *log_line, void *param)
{
    struct logging_state *l_state = param;
    l_state->saw_THANKYOU |= (strcmp("THANKYOU", log_line) == 0);
    l_state->output_was_produced |= (log_line[0] != '\0');
    log("%s", log_line);
    return log_line;
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
        problem_data_free(problem_data);
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

        if (config->ec_sending_sensitive_data)
        {
            /* We assume this event is a reporter */

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

        if (config->ec_sending_sensitive_data)
        {
            /* We assume this event is a reporter */

            /* Is problem non-reportable? */
            problem_data = load_problem_data_if_not_yet(problem_data, dump_dir_name);
            if (!problem_data)
                goto ret;
            if (is_not_reportable(problem_data))
                goto ret;

            char *msg = xasprintf(_("Event '%s' requires permission to send possibly sensitive data."
                                    " Do you want to continue?"),
                        ec_get_screen_name(config) ? ec_get_screen_name(config) : event_name);
            bool ok = ask_yesno(msg);
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

    unsigned picked;
    unsigned ii;
    for (ii = 0; ii < 3; ++ii)
    {
        char answer[16];

        read_from_stdin(_("Select an event to run: "), answer, sizeof(answer));
        if (!*answer)
            continue;

        picked = xatou(answer);
        if (picked <= count && picked != 0)
            break;

        printf("%s\n", _("You have chosen number out of range"));
    }

    if (ii == 3)
        error_msg_and_die(_("Invalid input, exiting."));

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

/*
 * Run event from chain. Perform the following steps for each event from chain.
 * 1. Terminates a chain run if a backtrace is not usable.
 * 2. Asks for missing settings.
 * 3. Performs review of problem data if event requires review.
 * 4. Runs events commands.
 * 5. Terminates a chain run if any event from the chain requires termination
 *    (prints THANKYOU to stdout in the current implementation)
 * 6. Terminates a chain run if any error occurs.
 * 7. Continues with processing of next event.
 */
int run_event_chain(const char *dump_dir_name, GList *chain, int interactive)
{
    struct logging_state l_state;

    struct run_event_state *run_state = new_run_event_state();
    run_state->logging_callback = do_log_and_check_for_THANKYOU;
    run_state->logging_param = &l_state;

    int retval = 0;
    for (GList *eitem = chain; eitem; eitem = g_list_next(eitem))
    {
        l_state.saw_THANKYOU = 0;
        l_state.output_was_produced = 0;
        const char *event_name = eitem->data;
        retval = interactive
                ? run_event_on_dir_name_interactively(run_state, dump_dir_name, event_name)
                : run_event_on_dir_name_batch(run_state, dump_dir_name, event_name)
                ;

        if (retval < 0)
            /* Nothing was run (bad backtrace, user declined, etc... */
            break;
        if (retval == 0 && run_state->children_count == 0)
        {
            printf("Error: no processing is specified for event '%s'", event_name);
            retval = 1;
        }
        else
        /* If program failed, or if it finished successfully without saying anything... */
        if (retval != 0 || !l_state.output_was_produced)
        {
            if (WIFSIGNALED(run_state->process_status))
                printf("(killed by signal %u)\n", WTERMSIG(run_state->process_status));
            else
                printf("(exited with %u)\n", retval);
        }
        if (retval != 0)
            break;
        if (l_state.saw_THANKYOU)
            break;
    }

    free_run_event_state(run_state);

    return retval;
}
