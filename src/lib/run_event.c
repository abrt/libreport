/*
    Copyright (C) 2009  Zdenek Prikryl (zprikryl@redhat.com)
    Copyright (C) 2009  RedHat inc.

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
#include <glob.h>
#include <regex.h>
#include "client.h"
#include "internal_libreport.h"

static char *run_event_stdio_log(char *log_line, void *param);
static void run_event_stdio_error_and_die(const char *error_line, void *param);

struct run_event_state *new_run_event_state()
{
    struct run_event_state *state = g_new0(struct run_event_state, 1);

    state->logging_callback = run_event_stdio_log;
    state->error_callback = run_event_stdio_error_and_die;

    state->alert_callback = run_event_stdio_alert;
    state->ask_callback = run_event_stdio_ask;
    state->ask_yes_no_callback = run_event_stdio_ask_yes_no;
    state->ask_yes_no_yesforever_callback= run_event_stdio_ask_yes_no_yesforever;
    state->ask_yes_no_save_result_callback= run_event_stdio_ask_yes_no_save_result;
    state->ask_password_callback = run_event_stdio_ask_password;

    state->extra_environment = g_ptr_array_new_with_free_func(g_free);

    state->command_output = libreport_strbuf_new();

    return state;
}

void free_run_event_state(struct run_event_state *state)
{
    if (state)
    {
        g_ptr_array_free(state->extra_environment, TRUE);
        libreport_strbuf_free(state->command_output);
        free_commands(state);
        free(state);
    }
}

void make_run_event_state_forwarding(struct run_event_state *state)
{
    /* reset callbacks, just to be sure */
    state->alert_callback = run_event_stdio_alert;
    state->ask_callback = run_event_stdio_ask;
    state->ask_yes_no_callback = run_event_stdio_ask_yes_no;
    state->ask_yes_no_yesforever_callback= run_event_stdio_ask_yes_no_yesforever;
    state->ask_yes_no_save_result_callback= run_event_stdio_ask_yes_no_save_result;
    state->ask_password_callback = run_event_stdio_ask_password;

    /*
     * Not sure if we should reset even logging_callback and error_callback?
     */

    libreport_xsetenv("REPORT_CLIENT_SLAVE", "1");
}

/* Asynchronous command execution */

/* It is not yet clear whether we need to re-parse event config file
 * and re-check the elements in dump dir after each command.
 *
 * Consider this config file:
 *
 * EVENT=e         cmd1
 * EVENT=e foo=bar cmd2
 * EVENT=e foo=baz cmd3
 *
 * Imagine that element foo existed and was equal to bar at the beginning.
 * After cmd1, should we execute cmd2 if element foo disappeared?
 * After cmd1/2, should we execute cmd3 if element foo changed value to baz?
 *
 * We used to read entire config file and select a list of commands to execute,
 * checking all conditions in the beginning.
 *
 * This proved to be bad for use cases where, for example, post-create rule
 * for a specified package needs to run:
 *
 * EVENT=post-create
 *      abrt-action-save-package-data
 * EVENT=post-create component=mypkg
 *      do_something
 *
 * Problem here is that "component" element is created by
 * abrt-action-save-package-data! Pre-selecting rules excludes second rule.
 *
 * Now we read entire config but do NOT select commands to execute,
 * we check conditions of every rule *directly before its execution*.
 * We remove the rule which we executed, and if the execution was successful,
 * we check conditions of every rule *starting from first rule*,
 * not *from the rule next to one we just executed and removed*.
 * IOW: the reordered rules like the example below work too:
 *
 * EVENT=post-create foo=bar do_something
 * EVENT=post-create         echo foo >bar
 *
 * List of commands machinery is encapsulated in struct run_event_state,
 * and public async API:
 *      prepare_commands(state);
 *      spawn_next_command(state, dir, event, 0);
 *      free_commands(state);
 * does not expose the way we select rules to execute.
 */

void free_rule_list(GList *rule_list)
{
    while (rule_list)
    {
        struct rule *cur_rule = rule_list->data;
        libreport_list_free_with_free(cur_rule->conditions);
        free(cur_rule->command);
        free(cur_rule);

        GList *next = rule_list->next;
        g_list_free_1(rule_list);
        rule_list = next;
    }
}

/* Stop-gap measure against infinite recursion */
#define MAX_recursion_depth 32

GList *load_rule_list(GList *rule_list,
                const char *conf_file_name,
                unsigned recursion_depth
) {
    FILE *conffile = fopen(conf_file_name, "r");
    if (!conffile)
    {
        error_msg("Can't open '%s'", conf_file_name);
        return rule_list;
    }

    /* Used only for better warning message */
    int line_counter = 0;
    /* Read and remember rules */
    char *next_line;
    while (1)
    {
        next_line = libreport_xmalloc_fgetline(conffile);
        if(!next_line)
        {
            log_parser("EOF");
            break;
        }

        if (*next_line == '\0' || *next_line == '#')
        {
            log_parser("empty or comment, skipping");
            free(next_line);
            continue;
        }

        log_parser("current line '%s'", next_line);
        /* Read and concatenate all lines in a rule */
        char *line = next_line;
        ++line_counter;

        if(strncmp(line, "EVENT", strlen("EVENT")) == 0)
        {
            log_parser("found EVENT");

            while (1)
            {
                long prev = ftell(conffile);
                if (prev < 0)
                    perror_msg_and_die("ftell");

                next_line = libreport_xmalloc_fgetline(conffile);

                log_parser("next_line is: '%s'", next_line ? next_line : "EOF");
                /* stop merging new lines into this event
                 * if we reach
                 * EOF
                 * line starting with:
                 ** # (comment)
                 ** EVENT
                 ** include
                 *
                 * When adding another directive don't forget to add it to this if!
                 */
                if (    !next_line
                     || *next_line == '#'
                     || strncmp(next_line, "EVENT", strlen("EVENT")) == 0
                     || strncmp(next_line, "include", strlen("include")) == 0
                ){
                    log_parser("found next EVENT or EOF, seeking back to prev line");

                    if (fseek(conffile, prev, SEEK_SET) < 0)
                        perror_msg_and_die("fseek");

                    free(next_line);
                    break;
                }

                ++line_counter;
                char *tmp = g_strdup_printf("%s\n%s", line, next_line);
                free(line);
                free(next_line);
                line = tmp;
            }

            char *p = libreport_skip_whitespace(line);

            /* Rule has form: [VAR=VAL]... PROG [ARGS] */
            struct rule *cur_rule = g_new0(struct rule, 1);

            while (1) /* word loop */
            {
                log_parser("word loop, p is: '%s'", p);
                char *end_word = libreport_skip_non_whitespace(p);
                log_parser("end word: '%s'", end_word);

                /* If there is no '=' in this word... */
                char *line_val = strchr(p, '=');
                log_parser("line val: '%s'", line_val);
                if (!line_val || line_val >= end_word)
                {
                    log_parser("found start of a command");
                    p = libreport_skip_whitespace(p);
                    break; /* ...we found the start of a command */
                }

                char *const current_word = g_strndup(p, end_word - p);
                cur_rule->conditions = g_list_append(cur_rule->conditions, current_word);
                log_parser("adding condition '%s'", current_word);

                /* Go to next word, but don't cross lines */
                p = libreport_skip_blank(end_word);
            } /* end of word loop */

            if (g_list_length(cur_rule->conditions) == 0)
            {
                log_debug("Adding '%s' without conditions", p);
            }
            else if (libreport_g_verbose >= 3)
            {
                log_warning("Adding '%s'\nwith conditions:", p);
                for (GList *c = cur_rule->conditions; c != NULL; c = g_list_next(c))
                    log_warning("| %s", (const char *)c->data);
            }

            cur_rule->command = g_strdup(p);

            rule_list = g_list_append(rule_list, cur_rule);
        }
        else if (   recursion_depth < MAX_recursion_depth
                 && strncmp(line, "include", strlen("include")) == 0
                 && isblank(line[strlen("include")])
        ) {
            log_parser("found include");

            /* "include GLOB_PATTERN" */
            const char *p = libreport_skip_whitespace(line + strlen("include"));

            const char *last_slash;
            g_autofree char *name_to_glob = NULL;
            if (*p != '/'
             && (last_slash = strrchr(conf_file_name, '/')) != NULL
            )
                /* GLOB_PATTERN is relative, and this include is in path/to/file.conf
                 * Construct path/to/GLOB_PATTERN:
                 */
                name_to_glob = g_strdup_printf("%.*s%s", (int)(last_slash - conf_file_name + 1), conf_file_name, p);
            else
                /* Either GLOB_PATTERN is absolute, or this include is in file.conf
                 * (no slashes in its name). Use unchanged GLOB_PATTERN:
                 */
                name_to_glob = g_strdup(p);

            glob_t globbuf;
            memset(&globbuf, 0, sizeof(globbuf));
            log_parser("globbing '%s'", name_to_glob);
            glob(name_to_glob, 0, NULL, &globbuf);
            char **name = globbuf.gl_pathv;
            if (name) while (*name)
            {
                log_parser("recursing into '%s'", *name);
                rule_list = load_rule_list(rule_list, *name, recursion_depth + 1);
                log_parser("returned from '%s'", *name);
                name++;
            }
            globfree(&globbuf);
        }
        else
            log_parser("Unknown line found, ignoring: '%s'", line);

        free(line);
    } /* end of line loop */

    fclose(conffile);

    return rule_list;
}

static int regcmp_lines(char *val, const char *regex)
{
    regex_t rx;
    int r = regcomp(&rx, regex, REG_NOSUB); //TODO: and REG_EXTENDED?
    //log_warning("REGEX:'%s':%d", regex, r);
    if (r)
    {
        //char errbuf[256];
        //size_t needsz = regerror(r, &rx, errbuf, sizeof(errbuf));
        error_msg("Bad regexp '%s'", regex); // TODO: use errbuf?
        return r;
    }

    /* Check every line */
    while (1)
    {
        char *eol = strchr(val, '\n');
        if (eol)
            *eol = '\0';
        r = regexec(&rx, val, 0, NULL, /*eflags:*/ 0);
        //log_warning("REGCMP:'%s':%d", val, r);
        if (eol)
            *eol = '\n';
        if (r == 0 || !eol)
            break;
        val = eol + 1;
    }
    /* Here, r == 0 if match was found */
    regfree(&rx);
    return r;
}

/* Checks rules in *pp_rule_list, starting from first (remaining) rule,
 * until it finds a rule with all conditions satisfied.
 * In this case, it deletes this rule and returns this rule's cmd.
 * Else (if it didn't find such rule), it returns NULL.
 * In case of error (dump_dir can't be opened), returns NULL.
 *
 * Intended usage:
 * list = load_rule_list(...);
 * while ((cmd = pop_next_command(&list, ...)) != NULL)
 *     run(cmd);
 */
static char* pop_next_command(GList **pp_rule_list,
        char **pp_event_name,    /* reports EVENT value thru this, if not NULL on entry */
        struct dump_dir **pp_dd, /* use *pp_dd for access to dump dir, if non-NULL */
        problem_data_t *pd,      /* use *pd for access to problem data, if non-NULL */
        const char *dump_dir_name,
        const char *pfx,
        unsigned pfx_len
)
{
    /* It is an error to pass both, but we can recover from it and use only
     * problem_data_t in that case */
    if (pp_dd != NULL && pd != NULL)
        error_msg("BUG: both dump dir and problem data passed to %s()", __func__);

    char *command = NULL;
    struct dump_dir *dd = pp_dd ? *pp_dd : NULL;

    GList *rule_list = *pp_rule_list;
    while (rule_list)
    {
        struct rule *cur_rule = rule_list->data;

        GList *condition = cur_rule->conditions;
        while (condition)
        {
            const char *cond_str = condition->data;
            const char *eq_sign = strchr(cond_str, '=');

            /* Is it "EVENT=foo"? */
            if (strncmp(cond_str, "EVENT=", 6) == 0)
            {
                if (strncmp(eq_sign + 1, pfx, pfx_len) != 0)
                    goto next_rule; /* prefix doesn't match */
                if (pp_event_name)
                {
                    free(*pp_event_name);
                    *pp_event_name = g_strdup(eq_sign + 1);
                }
            }
            else
            {
                /* Read from dump dir and compare */
                if (!dd && pd == NULL)
                {
                    /* Without dir to match, we assume match for all conditions */
                    if (!dump_dir_name)
                        goto next_cond;
                    dd = dd_opendir(dump_dir_name, /*flags:*/ DD_OPEN_READONLY);
                    if (!dd)
                    {
                        free_rule_list(rule_list);
                        *pp_rule_list = NULL;
                        goto ret; /* error (note: dd_opendir logged error msg) */
                    }
                }
                /* Is it "VAR~=REGEX"? */
                int regex = (eq_sign > cond_str && eq_sign[-1] == '~');
                /* Is it "VAR!=VAL"? */
                int inverted = (eq_sign > cond_str && eq_sign[-1] == '!');
                g_autofree char *var_name = g_strndup(cond_str, eq_sign - cond_str - (regex|inverted));
                char *real_val = NULL;
                char *free_me = NULL;
                if (pd == NULL)
                    free_me = real_val = dd_load_text_ext(dd, var_name, DD_FAIL_QUIETLY_ENOENT);
                else
                {
                    real_val = problem_data_get_content_or_NULL(pd, var_name);
                    if (real_val == NULL)
                        free_me = real_val = g_strdup("");
                }
                int vals_differ = regex ? regcmp_lines(real_val, eq_sign + 1) : strcmp(real_val, eq_sign + 1);
                free(free_me);
                if (inverted)
                    vals_differ = !vals_differ;

                /* Do values match? */
                if (vals_differ) /* no */
                {
                    goto next_rule;
                }
            }
 next_cond:
            /* We are here if current condition is satisfied */

            condition = condition->next;
        } /* while (condition) */
        /* We are here if all conditions are satisfied */
        /* IOW, we found rule to run, delete it and return its command */
        *pp_rule_list = g_list_remove(*pp_rule_list, cur_rule);
        libreport_list_free_with_free(cur_rule->conditions);
        command = cur_rule->command;
        /*free(cur_rule->command); - WRONG! we are returning it! */
        free(cur_rule);
        break;

 next_rule:
        rule_list = rule_list->next;
    } /* while (rule_list) */

 ret:
    if (pp_dd)
        *pp_dd = dd;
    else
        dd_close(dd);
    return command;
}

void free_commands(struct run_event_state *state)
{
    free_rule_list(state->rule_list);
    state->rule_list = NULL;
    state->command_out_fd = -1;
    state->command_pid = 0;
}

int prepare_commands(struct run_event_state *state)
{
    free_commands(state);

    state->children_count = 0;
    libreport_strbuf_clear(state->command_output);

    GList *rule_list = load_rule_list(NULL, CONF_DIR"/report_event.conf", /*recursion_depth:*/ 0);
    state->rule_list = rule_list;
    return rule_list != NULL;
}

int spawn_next_command(struct run_event_state *state,
                const char *dump_dir_name,
                const char *event,
                unsigned execflags
) {
    char *cmd = pop_next_command(&state->rule_list,
                NULL,          /* don't return event_name */
                NULL,          /* NULL dd: we match by... */
                NULL,          /* no problem data */
                dump_dir_name, /* ...dirname */
                event, strlen(event)+1 /* for this event name exactly (not prefix) */
    );
    if (!cmd)
        return -1;

    /* We count it even if fork fails. The counter isn't meant
     * to count *successful* forks, it is meant to let caller know
     * whether the event we run has *any* handlers configured, or not.
     */
    state->children_count++;

    log_info("Next command: '%s'", cmd);

    /* Just exporting dump_dir_name isn't always ok: it can be "."
     * and some children want to cd to other directory but still
     * be able to find problem directory by using $DUMP_DIR...
     */
    char *full_name = realpath(dump_dir_name, NULL);
    /* Export some useful environment variables for children */
    GPtrArray *env_array;

    env_array = g_ptr_array_new();

    g_ptr_array_add(env_array, g_strdup_printf("DUMP_DIR=%s", (full_name ? full_name : dump_dir_name)));
    g_ptr_array_add(env_array, g_strdup_printf("EVENT=%s", event));
    g_ptr_array_add(env_array, g_strdup_printf("REPORT_CLIENT_SLAVE=1"));
    for (unsigned int i = 0; i < state->extra_environment->len; i++)
    {
        char *variable;

        variable = g_ptr_array_index(state->extra_environment, i);

        g_ptr_array_add(env_array, g_strdup(variable));
    }
    g_ptr_array_add(env_array, NULL);

    free(full_name);

    char *argv[4];
    argv[0] = (char*)"/bin/sh"; // TODO: honor $SHELL?
    argv[1] = (char*)"-c";
    argv[2] = cmd;
    argv[3] = NULL;

    int pipefds[2];
    state->command_pid = libreport_fork_execv_on_steroids(
                EXECFLG_INPUT | EXECFLG_OUTPUT | EXECFLG_ERR2OUT | execflags,
                argv,
                pipefds,
                /* env_vec: */ (char **)env_array->pdata,
                /* dir: */ dump_dir_name,
                /* uid(unused): */ 0
    );
    state->command_out_fd = pipefds[0];
    state->command_in_fd = pipefds[1];

    g_ptr_array_free(env_array, TRUE);
    free(cmd);

    return 0;
}

int consume_event_command_output(struct run_event_state *state, const char *dump_dir_name)
{
    int r = 0;
    char buf[256];
    errno = 0;
    struct strbuf *cmd_output = state->command_output;
    while ((r = libreport_safe_read(state->command_out_fd, buf, sizeof(buf) - 1)) > 0)
    {
        char *newline;
        char *raw;
        buf[r] = '\0';
        raw = buf;

        while ((newline = strchr(raw, '\n')) != NULL)
        {
            *newline = '\0';
            libreport_strbuf_append_str(cmd_output, raw);
            char *msg = cmd_output->buf;

            g_autofree char *response = NULL;

            /* just cut off prefix, no waiting */
            if (libreport_prefixcmp(msg, REPORT_PREFIX_ALERT) == 0)
            {
                state->alert_callback(msg + sizeof(REPORT_PREFIX_ALERT) - 1 , state->interaction_param);
            }
            /* wait for y/N/f response on the same line */
            else if (libreport_prefixcmp(msg, REPORT_PREFIX_ASK_YES_NO_YESFOREVER) == 0)
            {
                /* example:
                 *   ASK_YES_NO_YESFOREVER ask_before_delete Do you want to delete selected files?
                 */
                char *key = msg + sizeof(REPORT_PREFIX_ASK_YES_NO_YESFOREVER) - 1;
                char *key_end = strchr(key, ' ');

                bool ans = false;

                if (!key_end)
                {   /* example:
                     *  ASK_YES_NO_YESFOREVER Continue?
                     *
                     * Print a wraning only and do not scary users with error messages.
                     */
                    log_warning("invalid input format (missing option name), using simple ask yes/no");

                    /* can't simply use 'goto ask_yes_no' because of different lenght of prefixes */
                    ans = state->ask_yes_no_callback(key, state->interaction_param);
                }
                else
                {
                    key_end[0] = '\0'; /* split 'key msg' to 'key' and 'msg' */
                    ans = state->ask_yes_no_yesforever_callback(key, key + strlen(key) + 1, state->interaction_param);
                    key_end[0] = ' '; /* restore original message, not sure if it is necessary */
                }

                response = g_strdup(ans ? "y" : "N");
            }
            /* wait for y/N/f/e response on the same line */
            else if (libreport_prefixcmp(msg, REPORT_PREFIX_ASK_YES_NO_SAVE_RESULT) == 0)
            {
                /* example:
                 *   ASK_YES_NO_SAVE_RESULT ask_before_delete Do you want to delete selected files?
                 */
                char *key = msg + sizeof(REPORT_PREFIX_ASK_YES_NO_SAVE_RESULT) - 1;
                char *key_end = strchr(key, ' ');

                bool ans = false;

                if (!key_end)
                {   /* example:
                     *  ASK_YES_NO_YESFOREVER Continue?
                     *
                     * Print a wraning only and do not scary users with error messages.
                     */
                    log_warning("invalid input format (missing option name), using simple ask yes/no");

                    /* can't simply use 'goto ask_yes_no' because of different lenght of prefixes */
                    ans = state->ask_yes_no_callback(key, state->interaction_param);
                }
                else
                {
                    key_end[0] = '\0'; /* split 'key msg' to 'key' and 'msg' */
                    ans = state->ask_yes_no_save_result_callback(key, key + strlen(key) + 1, state->interaction_param);
                    key_end[0] = ' '; /* restore original message, not sure if it is necessary */
                }

                response = g_strdup(ans ? "y" : "N");
            }
            /* wait for y/N response on the same line */
            else if (libreport_prefixcmp(msg, REPORT_PREFIX_ASK_YES_NO) == 0)
            {
                const bool ans = state->ask_yes_no_callback(msg + sizeof(REPORT_PREFIX_ASK_YES_NO) - 1, state->interaction_param);
                response = g_strdup(ans ? "y" : "N");
            }
            /* wait for the string on the same line */
            else if (libreport_prefixcmp(msg, REPORT_PREFIX_ASK) == 0)
            {
                response = state->ask_callback(msg + sizeof(REPORT_PREFIX_ASK) - 1, state->interaction_param);
            }
            /* set echo off and wait for password on the same line */
            else if (libreport_prefixcmp(msg, REPORT_PREFIX_ASK_PASSWORD) == 0)
            {
                response = state->ask_password_callback(msg + sizeof(REPORT_PREFIX_ASK_PASSWORD) - 1, state->interaction_param);
            }
            /* no special prefix -> forward to log if applicable
             * note that callback may take ownership of buf by returning NULL */
            else if (state->logging_callback)
            {
                char *logged = state->logging_callback(g_strdup(msg), state->logging_param);
                free(logged);
            }

            if (response)
            {
                size_t len = strlen(response);
                response[len++] = '\n';

                if (libreport_full_write(state->command_in_fd, response, len) != len)
                {
                    if (state->error_callback)
                        state->error_callback("<WRITE ERROR>", state->error_param);
                    else
                        perror_msg_and_die("Can't write %zu bytes to child's stdin", len);
                }
            }

            libreport_strbuf_clear(cmd_output);

            /* jump to next line */
            raw = newline + 1;
        }

        /* beginning of next line. the line continues by next read() */
        libreport_strbuf_append_str(cmd_output, raw);
    }

    /* Hope that child's stdout fd was set to O_NONBLOCK */
    if (r == -1 && errno == EAGAIN)
        return -1;

    libreport_strbuf_clear(cmd_output);

    /* Wait for child to actually exit, collect status */
    libreport_safe_waitpid(state->command_pid, &(state->process_status), 0);

    int retval = WEXITSTATUS(state->process_status);
    if (WIFSIGNALED(state->process_status))
        retval = WTERMSIG(state->process_status) + 128;

    if (retval == 0 && state->post_run_callback)
        retval = state->post_run_callback(dump_dir_name, state->post_run_param);

    return retval;
}

/* Synchronous command execution:
 */
int run_event_on_dir_name(struct run_event_state *state,
                const char *dump_dir_name,
                const char *event
) {
    prepare_commands(state);

    /* Execute every command in shell */

    int retval = 0;
    while (spawn_next_command(state, dump_dir_name, event, /*execflags:*/ 0) >= 0)
    {
        retval = consume_event_command_output(state, dump_dir_name);
        if (retval != 0)
            break;
    }

    free_commands(state);

    return retval;
}

int run_event_on_problem_data(struct run_event_state *state, problem_data_t *data, const char *event)
{
    state->children_count = 0;

    struct dump_dir *dd = create_dump_dir_from_problem_data(data, NULL);
    if (!dd)
        return -1;
    g_autofree char *dir_name = g_strdup(dd->dd_dirname);
    dd_close(dd);

    int r = run_event_on_dir_name(state, dir_name, event);

    g_hash_table_remove_all(data);
    dd = dd_opendir(dir_name, /*flags:*/ 0);
    if (dd)
    {
        problem_data_load_from_dump_dir(data, dd, NULL);
        dd_delete(dd);
    }

    return r;
}


static char *_list_possible_events(struct dump_dir **dd, problem_data_t *pd, const char *dump_dir_name, const char *pfx)
{
    struct strbuf *result = libreport_strbuf_new();

    GList *rule_list = load_rule_list(NULL, CONF_DIR"/report_event.conf", /*recursion_depth:*/ 0);

    unsigned pfx_len = strlen(pfx);
    for (;;)
    {
        /* Retrieve each cmd, and fetch its EVENT=foo value */
        char *event_name = NULL;
        char *cmd = pop_next_command(&rule_list,
                &event_name,       /* return event_name */
                dd,                /* match this dd... */
                pd,                /* no problem data */
                dump_dir_name,     /* ...or if NULL, this dirname */
                pfx, pfx_len       /* for events with this prefix */
        );
        if (!cmd)
        {
            free_rule_list(rule_list);
            free(event_name);
            break;
        }
        free(cmd);

        if (event_name)
        {
            /* Append "EVENT\n" - only if it is not there yet */
            unsigned e_len = strlen(event_name);
            char *p = result->buf;
            while (p && *p)
            {
                if (strncmp(p, event_name, e_len) == 0 && p[e_len] == '\n')
                    goto skip; /* This event is already in the result */
                p = strchr(p, '\n');
                if (p)
                    p++;
            }
            libreport_strbuf_append_strf(result, "%s\n", event_name);
 skip:
            free(event_name);
        }
    }

    return libreport_strbuf_free_nobuf(result);
}

char *list_possible_events(struct dump_dir *dd, const char *dump_dir_name, const char *pfx)
{
    return _list_possible_events((dd ? &dd : NULL), NULL, dump_dir_name, pfx);
}

char *list_possible_events_problem_data(problem_data_t *pd, const char *dump_dir_name, const char *pfx)
{
    return _list_possible_events(NULL, pd, dump_dir_name, pfx);
}

GList *list_possible_events_glist(const char *problem_dir_name,
                                  const char *pfx)
{
    struct dump_dir *dd = dd_opendir(problem_dir_name, DD_OPEN_READONLY);
    char *events = list_possible_events(dd, problem_dir_name, pfx);
    GList *l = libreport_parse_delimited_list(events, "\n");
    dd_close(dd);
    free(events);

    return l;
}

GList *list_possible_events_problem_data_glist(problem_data_t *pd,
                                  const char *problem_dir_name,
                                  const char *pfx)
{
    char *events = list_possible_events_problem_data(pd, problem_dir_name, pfx);
    GList *l = libreport_parse_delimited_list(events, "\n");
    free(events);

    return l;
}

void run_event_stdio_alert(const char *msg, void *param)
{
    libreport_alert(msg);
}

char *run_event_stdio_ask(const char *msg, void *param)
{
    return libreport_ask(msg);
}

int run_event_stdio_ask_yes_no(const char *msg, void *param)
{
    return libreport_ask_yes_no(msg);
}

int run_event_stdio_ask_yes_no_yesforever(const char *key, const char *msg, void *param)
{
    return libreport_ask_yes_no_yesforever(key, msg);
}

int run_event_stdio_ask_yes_no_save_result(const char *key, const char *msg, void *param)
{
    return libreport_ask_yes_no_save_result(key, msg);
}

char *run_event_stdio_ask_password(const char *msg, void *param)
{
    return libreport_ask_password(msg);
}

static char *run_event_stdio_log(char *log_line, void *param)
{
    libreport_client_log(log_line);
    return log_line;
}

static void run_event_stdio_error_and_die(const char *error_line, void *param)
{
    error_msg_and_die("Can't write bytes to child's stdin");
}

char *libreport_exit_status_as_string(const char *progname, int status)
{
    INITIALIZE_LIBREPORT();

    char *msg;
    if (WIFSIGNALED(status))
        msg = g_strdup_printf(_("('%s' was killed by signal %u)\n"), progname, WTERMSIG(status));
    else if (status == 0)
        msg = g_strdup_printf(_("('%s' completed successfully)\n"), progname);
    else
        msg = g_strdup_printf(_("('%s' exited with %u)\n"), progname, WEXITSTATUS(status));
    return msg;
}
