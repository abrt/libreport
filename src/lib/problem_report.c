/*
    Copyright (C) 2014  ABRT team
    Copyright (C) 2014  RedHat Inc

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

#include "problem_report.h"
#include "internal_libreport.h"

#include <satyr/stacktrace.h>
#include <satyr/thread.h>
#include <satyr/abrt.h>

#include <assert.h>

#define DESTROYED_POINTER (void *)0xdeadbeef

/* FORMAT:
 * |%summary:: Hello, world
 * |Problem description:: %bare_comment
 * |
 * |Package:: package
 * |
 * |%attach: %binary, backtrace
 * |
 * |%additional_info::
 * |%reporter%
 * |User:: user_name,uid
 * |
 * |Directories:: root,cwd
 *
 * PARSED DATA (list of struct section_t):
 * {
 *   section_t {
 *      .name     = '%summary';
 *      .items    = { 'Hello, world' };
 *      .children = NULL;
 *   },
 *   section_t {
 *      .name     = '%attach'
 *      .items    = { '%binary', 'backtrace' };
 *      .children = NULL;
 *   },
 *   section_t {
 *      .name     = '%description'
 *      .items    = NULL;
 *      .children = {
 *        section_t {
 *          .name     = 'Problem description:';
 *          .items    = { '%bare_comment' };
 *          .children = NULL;
 *        },
 *        section_t {
 *          .name     = '';
 *          .items    = NULL;
 *          .children = NULL;
 *        },
 *        section_t {
 *          .name     = 'Package:';
 *          .items    = { 'package' };
 *          .children = NULL;
 *        },
 *      }
 *   },
 *   section_t {
 *      .name     = '%additional_info'
 *      .items    = { '%reporter%' };
 *      .children = {
 *        section_t {
 *          .name     = 'User:';
 *          .items    = { 'user_name', 'uid' };
 *          .children = NULL;
 *        },
 *        section_t {
 *          .name     = '';
 *          .items    = NULL;
 *          .children = NULL;
 *        },
 *        section_t {
 *          .name     = 'Directories:';
 *          .items    = { 'root', 'cwd' };
 *          .children = NULL;
 *        },
 *      }
 *   }
 * }
 */
struct section_t {
    char *name;      ///< name or output text (%summar, 'Package version:');
    GList *items;    ///< list of file names and special items (%reporter, %binar, ...)
    GList *children; ///< list of sub sections (struct section_t)
};

typedef struct section_t section_t;

static section_t *
section_new(const char *name)
{
    section_t *self = xmalloc(sizeof(*self));
    self->name = xstrdup(name);
    self->items = NULL;
    self->children = NULL;

    return self;
}

static void
section_free(section_t *self)
{
    if (self == NULL)
        return;

    free(self->name);
    g_list_free_full(self->items, free);
    g_list_free_full(self->children, (GDestroyNotify)section_free);

    free(self);
}

static int
section_name_cmp(section_t *lhs, const char *rhs)
{
    return strcmp((lhs->name + 1), rhs);
}

/* Utility functions */

static GList*
split_string_on_char(const char *str, char ch)
{
    GList *list = NULL;
    for (;;)
    {
        const char *delim = strchrnul(str, ch);
        list = g_list_prepend(list, xstrndup(str, delim - str));
        if (*delim == '\0')
            break;
        str = delim + 1;
    }
    return g_list_reverse(list);
}

static int
compare_item_name(const char *lookup, const char *name)
{
    if (lookup[0] == '-')
        lookup++;
    else if (strncmp(lookup, "%bare_", 6) == 0)
        lookup += 6;
    return strcmp(lookup, name);
}

static int
is_item_name_in_section(const section_t *lookup, const char *name)
{
    if (g_list_find_custom(lookup->items, name, (GCompareFunc)compare_item_name))
        return 0; /* "found it!" */
    return 1;
}

static bool is_explicit_or_forbidden(const char *name, GList *comment_fmt_spec);

static int
is_explicit_or_forbidden_child(const section_t *master_section, const char *name)
{
    if (is_explicit_or_forbidden(name, master_section->children))
        return 0; /* "found it!" */
    return 1;
}

/* For example: 'package' belongs to '%oneline', but 'package' is used in
 * 'Version of component', so it is not very helpful to include that file once
 * more in another section
 */
static bool
is_explicit_or_forbidden(const char *name, GList *comment_fmt_spec)
{
    return    g_list_find_custom(comment_fmt_spec, name, (GCompareFunc)is_item_name_in_section)
           || g_list_find_custom(comment_fmt_spec, name, (GCompareFunc)is_explicit_or_forbidden_child);
}

static GList*
load_stream(FILE *fp)
{
    assert(fp);

    GList *sections = NULL;
    section_t *master = section_new("%description");
    section_t *sec = NULL;

    sections = g_list_append(sections, master);

    char *line;
    while ((line = xmalloc_fgetline(fp)) != NULL)
    {
        /* Skip comments */
        char first = *skip_whitespace(line);
        if (first == '#')
            goto free_line;

        /* Handle trailing backslash continuation */
 check_continuation: ;
        unsigned len = strlen(line);
        if (len && line[len-1] == '\\')
        {
            line[len-1] = '\0';
            char *next_line = xmalloc_fgetline(fp);
            if (next_line)
            {
                line = append_to_malloced_string(line, next_line);
                free(next_line);
                goto check_continuation;
            }
        }

        /* We are reusing line buffer to form temporary
         * "key\0values\0..." in its beginning
         */
        bool summary_line = false;
        char *value = NULL;
        char *src;
        char *dst;
        for (src = dst = line; *src; src++)
        {
            char c = *src;
            /* did we reach the value list? */
            if (!value && c == ':' && src[1] == ':')
            {
                *dst++ = '\0'; /* terminate key */
                src += 1;
                value = dst; /* remember where value starts */
                summary_line = (strcmp(line, "%summary") == 0);
                if (summary_line)
                {
                    value = (src + 1);
                    break;
                }
                continue;
            }
            /* skip whitespace in value list */
            if (value && isspace(c))
                continue;
            *dst++ = c; /* store next key or value char */
        }

        GList *item_list = NULL;
        if (summary_line)
        {
            /* %summary is special */
            item_list = g_list_append(NULL, xstrdup(skip_whitespace(value)));
        }
        else
        {
            *dst = '\0'; /* terminate value (or key) */
            if (value)
                item_list = split_string_on_char(value, ',');
        }

        sec = section_new(line);
        sec->items = item_list;

        if (sec->name[0] == '%')
        {
            if (!summary_line && strcmp(sec->name, "%attach") != 0)
            {
                master->children = g_list_reverse(master->children);
                master = sec;
            }

            sections = g_list_prepend(sections, sec);
        }
        else
            master->children = g_list_prepend(master->children, sec);

 free_line:
        free(line);
    }

    /* If master equals sec, then master's children list was not yet reversed.
     *
     * %description is the default section (i.e is not explicitly mentioned)
     * and %summary nor %attach cause its children list to reverse.
     */
    if (master == sec || strcmp(master->name, "%description") == 0)
        master->children = g_list_reverse(master->children);

    return sections;
}


/* Summary generation */

#define MAX_OPT_DEPTH 10
static int
format_percented_string(const char *str, problem_data_t *pd, FILE *result)
{
    long old_pos[MAX_OPT_DEPTH] = { 0 };
    int okay[MAX_OPT_DEPTH] = { 1 };
    long len = 0;
    int opt_depth = 1;

    while (*str) {
        switch (*str) {
        default:
            putc(*str, result);
            len++;
            str++;
            break;
        case '\\':
            if (str[1])
                str++;
            putc(*str, result);
            len++;
            str++;
            break;
        case '[':
            if (str[1] == '[' && opt_depth < MAX_OPT_DEPTH)
            {
                old_pos[opt_depth] = len;
                okay[opt_depth] = 1;
                opt_depth++;
                str += 2;
            } else {
                putc(*str, result);
                len++;
                str++;
            }
            break;
        case ']':
            if (str[1] == ']' && opt_depth > 1)
            {
                opt_depth--;
                if (!okay[opt_depth])
                {
                    if (fseek(result, old_pos[opt_depth], SEEK_SET) < 0)
                        perror_msg_and_die("fseek");
                    len = old_pos[opt_depth];
                }
                str += 2;
            } else {
                putc(*str, result);
                len++;
                str++;
            }
            break;
        case '%': ;
            char *nextpercent = strchr(++str, '%');
            if (!nextpercent)
            {
                error_msg_and_die("Unterminated %%element%%: '%s'", str - 1);
            }

            *nextpercent = '\0';
            const problem_item *item = problem_data_get_item_or_NULL(pd, str);
            *nextpercent = '%';

            if (item && (item->flags & CD_FLAG_TXT))
            {
                fputs(item->content, result);
                len += strlen(item->content);
            }
            else
                okay[opt_depth - 1] = 0;
            str = nextpercent + 1;
            break;
        }
    }

    if (opt_depth > 1)
    {
        error_msg_and_die("Unbalanced [[ ]] bracket");
    }

    if (!okay[0])
    {
        error_msg("Undefined variable outside of [[ ]] bracket");
    }

    return 0;
}

/* BZ comment generation */

static int
append_text(struct strbuf *result, const char *item_name, const char *content, bool print_item_name)
{
    char *eol = strchrnul(content, '\n');
    if (eol[0] == '\0' || eol[1] == '\0')
    {
        /* one-liner */
        int pad = 16 - (strlen(item_name) + 2);
        if (pad < 0)
            pad = 0;
        if (print_item_name)
            strbuf_append_strf(result,
                    eol[0] == '\0' ? "%s: %*s%s\n" : "%s: %*s%s",
                    item_name, pad, "", content
            );
        else
            strbuf_append_strf(result,
                    eol[0] == '\0' ? "%s\n" : "%s",
                    content
            );
    }
    else
    {
        /* multi-line item */
        if (print_item_name)
            strbuf_append_strf(result, "%s:\n", item_name);
        for (;;)
        {
            eol = strchrnul(content, '\n');
            strbuf_append_strf(result,
                    /* For %bare_multiline_item, we don't want to print colons */
                    (print_item_name ? ":%.*s\n" : "%.*s\n"),
                    (int)(eol - content), content
            );
            if (eol[0] == '\0' || eol[1] == '\0')
                break;
            content = eol + 1;
        }
    }
    return 1;
}

static int
append_short_backtrace(struct strbuf *result, problem_data_t *problem_data, bool print_item_name, problem_report_settings_t *settings)
{
    const problem_item *backtrace_item = problem_data_get_item_or_NULL(problem_data,
                                                                       FILENAME_BACKTRACE);
    const problem_item *core_stacktrace_item = NULL;
    if (!backtrace_item || !(backtrace_item->flags & CD_FLAG_TXT))
    {
        backtrace_item = NULL;

        core_stacktrace_item = problem_data_get_item_or_NULL(problem_data,
                                                             FILENAME_CORE_BACKTRACE);

        if (!core_stacktrace_item || !(core_stacktrace_item->flags & CD_FLAG_TXT))
            return 0;
    }

    char *truncated = NULL;

    if (core_stacktrace_item || strlen(backtrace_item->content) >= settings->prs_shortbt_max_text_size)
    {
        log_debug("'backtrace' exceeds the text file size, going to append its short version");

        char *error_msg = NULL;
        const char *type = problem_data_get_content_or_NULL(problem_data, FILENAME_TYPE);
        if (!type)
        {
            log_debug("Problem data does not contain '"FILENAME_TYPE"' file");
            return 0;
        }

        /* For CCpp crashes, use the GDB-produced backtrace which should be
         * available by now. sr_abrt_type_from_type returns SR_REPORT_CORE
         * by default for CCpp crashes.
         */
        enum sr_report_type report_type = sr_abrt_type_from_type(type);
        if (backtrace_item && strcmp(type, "CCpp") == 0)
        {
            log_debug("Successfully identified 'CCpp' abrt type");
            report_type = SR_REPORT_GDB;
        }

        const char *content = backtrace_item ? backtrace_item->content : core_stacktrace_item->content;
        struct sr_stacktrace *backtrace = sr_stacktrace_parse(report_type, content, &error_msg);

        if (!backtrace)
        {
            log(_("Can't parse backtrace: %s"), error_msg);
            free(error_msg);
            return 0;
        }

        /* normalize */
        struct sr_thread *thread = sr_stacktrace_find_crash_thread(backtrace);
        sr_thread_normalize(thread);

        /* Get optimized thread stack trace for max_frames top most frames */
        truncated = sr_stacktrace_to_short_text(backtrace, settings->prs_shortbt_max_frames);
        sr_stacktrace_free(backtrace);

        if (!truncated)
        {
            log(_("Can't generate stacktrace description (no crash thread?)"));
            return 0;
        }
    }
    else
    {
        log_debug("'backtrace' is small enough to be included as is");
    }

    /* full item content  */
    append_text(result,
                /*item_name:*/ truncated ? "truncated_backtrace" : FILENAME_BACKTRACE,
                /*content:*/   truncated ? truncated             : backtrace_item->content,
                print_item_name
    );
    free(truncated);
    return 1;
}

static int
append_item(struct strbuf *result, const char *item_name, problem_data_t *pd, GList *comment_fmt_spec, problem_report_settings_t *settings)
{
    bool print_item_name = (strncmp(item_name, "%bare_", strlen("%bare_")) != 0);
    if (!print_item_name)
        item_name += strlen("%bare_");

    if (item_name[0] != '%')
    {
        struct problem_item *item = problem_data_get_item_or_NULL(pd, item_name);
        if (!item)
            return 0; /* "I did not print anything" */
        if (!(item->flags & CD_FLAG_TXT))
            return 0; /* "I did not print anything" */

        char *formatted = problem_item_format(item);
        char *content = formatted ? formatted : item->content;
        append_text(result, item_name, content, print_item_name);
        free(formatted);
        return 1; /* "I printed something" */
    }

    /* Special item name */

    /* Compat with previously-existed ad-hockery: %short_backtrace */
    if (strcmp(item_name, "%short_backtrace") == 0)
        return append_short_backtrace(result, pd, print_item_name, settings);

    /* Compat with previously-existed ad-hockery: %reporter */
    if (strcmp(item_name, "%reporter") == 0)
        return append_text(result, "reporter", PACKAGE"-"VERSION, print_item_name);

    /* %oneline,%multiline,%text */
    bool oneline   = (strcmp(item_name+1, "oneline"  ) == 0);
    bool multiline = (strcmp(item_name+1, "multiline") == 0);
    bool text      = (strcmp(item_name+1, "text"     ) == 0);
    if (!oneline && !multiline && !text)
    {
        log("Unknown or unsupported element specifier '%s'", item_name);
        return 0; /* "I did not print anything" */
    }

    int printed = 0;

    /* Iterate over _sorted_ items */
    GList *sorted_names = g_hash_table_get_keys(pd);
    sorted_names = g_list_sort(sorted_names, (GCompareFunc)strcmp);

    /* %text => do as if %oneline, then repeat as if %multiline */
    if (text)
        oneline = 1;

 again: ;
    GList *l = sorted_names;
    while (l)
    {
        const char *name = l->data;
        l = l->next;
        struct problem_item *item = g_hash_table_lookup(pd, name);
        if (!item)
            continue; /* paranoia, won't happen */

        if (!(item->flags & CD_FLAG_TXT))
            continue;

        if (is_explicit_or_forbidden(name, comment_fmt_spec))
            continue;

        char *formatted = problem_item_format(item);
        char *content = formatted ? formatted : item->content;
        char *eol = strchrnul(content, '\n');
        bool is_oneline = (eol[0] == '\0' || eol[1] == '\0');
        if (oneline == is_oneline)
            printed |= append_text(result, name, content, print_item_name);
        free(formatted);
    }
    if (text && oneline)
    {
        /* %text, and we just did %oneline. Repeat as if %multiline */
        oneline = 0;
        /*multiline = 1; - not checked in fact, so why bother setting? */
        goto again;
    }

    g_list_free(sorted_names); /* names themselves are not freed */

    return printed;
}

#define add_to_section_output(format, ...) \
    do { \
    for (; empty_lines > 0; --empty_lines) fputc('\n', result); \
    empty_lines = 0; \
    fprintf(result, format, __VA_ARGS__); \
    } while (0)

static void
format_section(section_t *section, problem_data_t *pd, GList *comment_fmt_spec, FILE *result, problem_report_settings_t *settings)
{
    int empty_lines = -1;

    for (GList *iter = section->children; iter; iter = g_list_next(iter))
    {
        section_t *child = (section_t *)iter->data;
        if (child->items)
        {
            /* "Text: item[,item]..." */
            struct strbuf *output = strbuf_new();
            GList *item = child->items;
            while (item)
            {
                const char *str = item->data;
                item = item->next;
                if (str[0] == '-') /* "-name", ignore it */
                    continue;
                append_item(output, str, pd, comment_fmt_spec, settings);
            }

            if (output->len != 0)
                add_to_section_output((child->name[0] ? "%s:\n%s" : "%s%s"),
                                      child->name, output->buf);

            strbuf_free(output);
        }
        else
        {
            /* Just "Text" (can be "") */

            /* Filter out trailint empty lines */
            if (child->name[0] != '\0')
                add_to_section_output("%s\n", child->name);
            /* Do not count empty lines, if output wasn't yet produced */
            else if (empty_lines >= 0)
                ++empty_lines;
        }
    }
}

static GList *
get_special_items(const char *item_name, problem_data_t *pd, GList *comment_fmt_spec)
{
    /* %oneline,%multiline,%text,%binary */
    bool oneline   = (strcmp(item_name+1, "oneline"  ) == 0);
    bool multiline = (strcmp(item_name+1, "multiline") == 0);
    bool text      = (strcmp(item_name+1, "text"     ) == 0);
    bool binary    = (strcmp(item_name+1, "binary"   ) == 0);
    if (!oneline && !multiline && !text && !binary)
    {
        log("Unknown or unsupported element specifier '%s'", item_name);
        return NULL;
    }

    log_debug("Special item_name '%s', iterating for attach...", item_name);
    GList *result = 0;

    /* Iterate over _sorted_ items */
    GList *sorted_names = g_hash_table_get_keys(pd);
    sorted_names = g_list_sort(sorted_names, (GCompareFunc)strcmp);

    GList *l = sorted_names;
    while (l)
    {
        const char *name = l->data;
        l = l->next;
        struct problem_item *item = g_hash_table_lookup(pd, name);
        if (!item)
            continue; /* paranoia, won't happen */

        if (is_explicit_or_forbidden(name, comment_fmt_spec))
            continue;

        if ((item->flags & CD_FLAG_TXT) && !binary)
        {
            char *content = item->content;
            char *eol = strchrnul(content, '\n');
            bool is_oneline = (eol[0] == '\0' || eol[1] == '\0');
            if (text || oneline == is_oneline)
                result = g_list_append(result, xstrdup(name));
        }
        else if ((item->flags & CD_FLAG_BIN) && binary)
            result = g_list_append(result, xstrdup(name));
    }

    g_list_free(sorted_names); /* names themselves are not freed */


    log_debug("...Done iterating over '%s' for attach", item_name);

    return result;
}

static GList *
get_attached_files(problem_data_t *pd, GList *items, GList *comment_fmt_spec)
{
    GList *result = NULL;
    GList *item = items;
    while (item != NULL)
    {
        const char *item_name = item->data;
        item = item->next;
        if (item_name[0] == '-') /* "-name", ignore it */
            continue;

        if (item_name[0] != '%')
        {
            result = g_list_append(result, xstrdup(item_name));
            continue;
        }

        GList *special = get_special_items(item_name, pd, comment_fmt_spec);
        if (special == NULL)
        {
            log_notice("No attachment found for '%s'", item_name);
            continue;
        }

        result = g_list_concat(result, special);
    }

    return result;
}

/*
 * Problem Report - memor stream
 *
 * A wrapper for POSIX memory stream.
 *
 * A memory stream is presented as FILE *.
 *
 * A memory stream is associated with a pointer to written data and a pointer
 * to size of the written data.
 *
 * This structure holds all of the used pointers.
 */
struct memstream_buffer
{
    char *msb_buffer;
    size_t msb_size;
    FILE *msb_stream;
};

static struct memstream_buffer *
memstream_buffer_new()
{
    struct memstream_buffer *self = xmalloc(sizeof(*self));

    self->msb_buffer = NULL;
    self->msb_stream = open_memstream(&(self->msb_buffer), &(self->msb_size));

    return self;
}

static void
memstream_buffer_free(struct memstream_buffer *self)
{
    if (self == NULL)
        return;

    fclose(self->msb_stream);
    self->msb_stream = DESTROYED_POINTER;

    free(self->msb_buffer);
    self->msb_buffer = DESTROYED_POINTER;

    free(self);
}

static FILE *
memstream_get_stream(struct memstream_buffer *self)
{
    assert(self != NULL);

    return self->msb_stream;
}

static const char *
memstream_get_string(struct memstream_buffer *self)
{
    assert(self != NULL);
    assert(self->msb_stream != NULL);

    fflush(self->msb_stream);

    return self->msb_buffer;
}


/*
 * Problem Report
 *
 * The formated strings are internaly stored in "buffer"s. If a programer wants
 * to get a formated section data, a getter function extracts those data from
 * the apropriate buffer and returns them in form of null-terminated string.
 *
 * Each section has own buffer.
 *
 * There are three common sections that are always present:
 * 1. summary
 * 2. description
 * 3. attach
 * Buffers of these sections has own structure member for the sake of
 * efficiency.
 *
 * The custom sections hash their buffers stored in a map where key is a
 * section's name and value is a section's buffer.
 *
 * Problem report provides the programers with the possibility to ammend
 * formated output to any section buffer.
 */
struct problem_report
{
    struct memstream_buffer *pr_sec_summ; ///< %summary buffer
    struct memstream_buffer *pr_sec_desc; ///< %description buffer
    GList            *pr_attachments;     ///< %attach - list of file names
    GHashTable       *pr_sec_custom;      ///< map : %(custom section) -> buffer
};

static problem_report_t *
problem_report_new()
{
    problem_report_t *self = xmalloc(sizeof(*self));

    self->pr_sec_summ = memstream_buffer_new();
    self->pr_sec_desc = memstream_buffer_new();
    self->pr_attachments = NULL;
    self->pr_sec_custom = NULL;

    return self;
}

static void
problem_report_initialize_custom_sections(problem_report_t *self)
{
    assert(self != NULL);
    assert(self->pr_sec_custom == NULL);

    self->pr_sec_custom = g_hash_table_new_full(g_str_hash, g_str_equal, free,
                                                (GDestroyNotify)memstream_buffer_free);
}

static void
problem_report_destroy_custom_sections(problem_report_t *self)
{
    assert(self != NULL);
    assert(self->pr_sec_custom != NULL);

    g_hash_table_destroy(self->pr_sec_custom);
}

static int
problem_report_add_custom_section(problem_report_t *self, const char *name)
{
    assert(self != NULL);

    if (self->pr_sec_custom == NULL)
    {
        problem_report_initialize_custom_sections(self);
    }

    if (problem_report_get_buffer(self, name))
    {
        log_warning("Custom section already exists : '%s'", name);
        return -EEXIST;
    }

    log_debug("Problem report enriched with section : '%s'", name);
    g_hash_table_insert(self->pr_sec_custom, xstrdup(name), memstream_buffer_new());
    return 0;
}

static struct memstream_buffer *
problem_report_get_section_buffer(const problem_report_t *self, const char *section_name)
{
    if (self->pr_sec_custom == NULL)
    {
        log_debug("Couldn't find section '%s': no custom section added", section_name);
        return NULL;
    }

    return (struct memstream_buffer *)g_hash_table_lookup(self->pr_sec_custom, section_name);
}

problem_report_buffer *
problem_report_get_buffer(const problem_report_t *self, const char *section_name)
{
    assert(self != NULL);
    assert(section_name != NULL);

    if (strcmp(PR_SEC_SUMMARY, section_name) == 0)
        return memstream_get_stream(self->pr_sec_summ);

    if (strcmp(PR_SEC_DESCRIPTION, section_name) == 0)
        return memstream_get_stream(self->pr_sec_desc);

    struct memstream_buffer *buf = problem_report_get_section_buffer(self, section_name);
    return buf == NULL ? NULL : memstream_get_stream(buf);
}

const char *
problem_report_get_summary(const problem_report_t *self)
{
    assert(self != NULL);

    return memstream_get_string(self->pr_sec_summ);
}

const char *
problem_report_get_description(const problem_report_t *self)
{
    assert(self != NULL);

    return memstream_get_string(self->pr_sec_desc);
}

const char *
problem_report_get_section(const problem_report_t *self, const char *section_name)
{
    assert(self != NULL);
    assert(section_name);

    struct memstream_buffer *buf = problem_report_get_section_buffer(self, section_name);

    if (buf == NULL)
        return NULL;

    return memstream_get_string(buf);
}

static void
problem_report_set_attachments(problem_report_t *self, GList *attachments)
{
    assert(self != NULL);
    assert(self->pr_attachments == NULL);

    self->pr_attachments = attachments;
}

GList *
problem_report_get_attachments(const problem_report_t *self)
{
    assert(self != NULL);

    return self->pr_attachments;
}

void
problem_report_free(problem_report_t *self)
{
    if (self == NULL)
        return;

    memstream_buffer_free(self->pr_sec_summ);
    self->pr_sec_summ = DESTROYED_POINTER;

    memstream_buffer_free(self->pr_sec_desc);
    self->pr_sec_desc = DESTROYED_POINTER;

    g_list_free_full(self->pr_attachments, free);
    self->pr_attachments = DESTROYED_POINTER;

    if (self->pr_sec_custom)
    {
        problem_report_destroy_custom_sections(self);
        self->pr_sec_custom = DESTROYED_POINTER;
    }

    free(self);
}

/*
 * Problem Formatter - extra section
 */
struct extra_section
{
    char *pfes_name;    ///< name with % prefix
    int   pfes_flags;   ///< whether is required or not
};

static struct extra_section *
extra_section_new(const char *name, int flags)
{
    struct extra_section *self = xmalloc(sizeof(*self));

    self->pfes_name = xstrdup(name);
    self->pfes_flags = flags;

    return self;
}

static void
extra_section_free(struct extra_section *self)
{
    if (self == NULL)
        return;

    free(self->pfes_name);
    self->pfes_name = DESTROYED_POINTER;

    free(self);
}

static int
extra_section_name_cmp(struct extra_section *lhs, const char *rhs)
{
    return strcmp(lhs->pfes_name, rhs);
}

/*
 * Problem Formatter
 *
 * Holds parsed sections lists.
 */
struct problem_formatter
{
    GList *pf_sections;         ///< parsed sections (struct section_t)
    GList *pf_extra_sections;   ///< user configured sections (struct extra_section)
    char  *pf_default_summary;  ///< default summary format
    problem_report_settings_t pf_settings; ///< settings for report generating
};

static problem_report_settings_t
problem_report_settings_init(void)
{
    problem_report_settings_t settings = {
        .prs_shortbt_max_frames = 10,
        .prs_shortbt_max_text_size = CD_TEXT_ATT_SIZE_BZ,
    };

    return settings;
}

problem_report_settings_t problem_formatter_get_settings(const problem_formatter_t *self)
{
    return self->pf_settings;
}

void problem_formatter_set_settings(problem_formatter_t *self, problem_report_settings_t settings)
{
    self->pf_settings = settings;
}

problem_formatter_t *
problem_formatter_new(void)
{
    problem_formatter_t *self = xzalloc(sizeof(*self));

    self->pf_default_summary = xstrdup("%reason%");
    self->pf_settings = problem_report_settings_init();

    return self;
}

void
problem_formatter_free(problem_formatter_t *self)
{
    if (self == NULL)
        return;

    g_list_free_full(self->pf_sections, (GDestroyNotify)section_free);
    self->pf_sections = DESTROYED_POINTER;

    g_list_free_full(self->pf_extra_sections, (GDestroyNotify)extra_section_free);
    self->pf_extra_sections = DESTROYED_POINTER;

    free(self->pf_default_summary);
    self->pf_default_summary = DESTROYED_POINTER;

    free(self);
}

static int
problem_formatter_is_section_known(problem_formatter_t *self, const char *name)
{
  return    strcmp(name, "summary")     == 0
         || strcmp(name, "attach")      == 0
         || strcmp(name, "description") == 0
         || NULL != g_list_find_custom(self->pf_extra_sections, name, (GCompareFunc)extra_section_name_cmp);
}

// i.e additional_info -> no flags
int
problem_formatter_add_section(problem_formatter_t *self, const char *name, int flags)
{
    /* Do not add already added sections */
    if (problem_formatter_is_section_known(self, name))
    {
        log_debug("Extra section already exists : '%s' ", name);
        return -EEXIST;
    }

    self->pf_extra_sections = g_list_prepend(self->pf_extra_sections,
                                             extra_section_new(name, flags));

    return 0;
}

// check format validity and produce warnings
static int
problem_formatter_validate(problem_formatter_t *self)
{
    int retval = 0;

    /* Go through all (struct extra_section)s and check whete those having flag
     * PFFF_REQUIRED are present in the parsed (struct section_t)s.
     */
    for (GList *iter = self->pf_extra_sections; iter; iter = g_list_next(iter))
    {
        struct extra_section *section = (struct extra_section *)iter->data;

        log_debug("Validating extra section : '%s'", section->pfes_name);

        if (   (PFFF_REQUIRED & section->pfes_flags)
            && NULL == g_list_find_custom(self->pf_sections, section->pfes_name, (GCompareFunc)section_name_cmp))
        {
            log_warning("Problem format misses required section : '%s'", section->pfes_name);
            ++retval;
        }
    }

    /* Go through all the parsed (struct section_t)s check whether are all
     * known, i.e. each section is either one of the common sections (summary,
     * description, attach) or is present in the (struct extra_section)s.
     */
    for (GList *iter = self->pf_sections; iter; iter = g_list_next(iter))
    {
        section_t *section = (section_t *)iter->data;

        if (!problem_formatter_is_section_known(self, (section->name + 1)))
        {
            log_warning("Problem format contains unrecognized section : '%s'", section->name);
            ++retval;
        }
    }

    return retval;
}

int
problem_formatter_load_string(problem_formatter_t *self, const char *fmt)
{
    const size_t len = strlen(fmt);
    if (len != 0)
    {
        FILE *fp = fmemopen((void *)fmt, len, "r");
        if (fp == NULL)
        {
            error_msg("Not enough memory to open a stream for reading format string.");
            return -ENOMEM;
        }

        self->pf_sections = load_stream(fp);
        fclose(fp);
    }

    return problem_formatter_validate(self);
}

int
problem_formatter_load_file(problem_formatter_t *self, const char *path)
{
    FILE *fp = stdin;
    if (strcmp(path, "-") != 0)
    {
        fp = fopen(path, "r");
        if (!fp)
            return -ENOENT;
    }

    self->pf_sections = load_stream(fp);

    if (fp != stdin)
        fclose(fp);

    return problem_formatter_validate(self);
}

// generates report
int
problem_formatter_generate_report(const problem_formatter_t *self, problem_data_t *data, problem_report_t **report)
{
    problem_report_settings_t settings = problem_formatter_get_settings(self);

    problem_report_t *pr = problem_report_new();

    for (GList *iter = self->pf_extra_sections; iter; iter = g_list_next(iter))
        problem_report_add_custom_section(pr, ((struct extra_section *)iter->data)->pfes_name);

    bool has_summary = false;
    for (GList *iter = self->pf_sections; iter; iter = g_list_next(iter))
    {
        section_t *section = (section_t *)iter->data;

        /* %summary is something special */
        if (strcmp(section->name, "%summary") == 0)
        {
            has_summary = true;
            format_percented_string((const char *)section->items->data, data,
                                    problem_report_get_buffer(pr, PR_SEC_SUMMARY));
        }
        /* %attach as well */
        else if (strcmp(section->name, "%attach") == 0)
        {
            problem_report_set_attachments(pr, get_attached_files(data, section->items, self->pf_sections));
        }
        else /* %description or a custom section (e.g. %additional_info) */
        {
            FILE *buffer = problem_report_get_buffer(pr, section->name + 1);

            if (buffer != NULL)
            {
                log_debug("Formatting section : '%s'", section->name);
                format_section(section, data, self->pf_sections, buffer, &settings);
            }
            else
                log_warning("Unsupported section '%s'", section->name);
        }
    }

    if (!has_summary) {
        log_debug("Problem format misses section '%%summary'. Using the default one : '%s'.",
                    self->pf_default_summary);

        format_percented_string(self->pf_default_summary,
                   data, problem_report_get_buffer(pr, PR_SEC_SUMMARY));
    }

    *report = pr;
    return 0;
}
