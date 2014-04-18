/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

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

static bool rejected_name(const char *name, char **v, int flags)
{
    bool r = is_in_string_list(name, v);
    if (flags & MAKEDESC_WHITELIST)
         r = !r;
    return r;
}

static
char *make_description_item_multiline(const char *name, const char *content)
{
    char *eol = strchr(content, '\n');
    if (!eol)
        return NULL;

    struct strbuf *buf = strbuf_new();
    strbuf_append_str(buf, name);
    strbuf_append_str(buf, ":\n");
    for (;;)
    {
        eol = strchrnul(content, '\n');
        strbuf_append_strf(buf, ":%.*s\n", (int)(eol - content), content);
        if (*eol == '\0' || eol[1] == '\0')
            break;
        content = eol + 1;
    }

    return strbuf_free_nobuf(buf);
}

char *make_description(problem_data_t *problem_data, char **names_to_skip,
                       unsigned max_text_size, unsigned desc_flags)
{
    INITIALIZE_LIBREPORT();

    struct strbuf *buf_dsc = strbuf_new();

    const char *analyzer = problem_data_get_content_or_NULL(problem_data,
                                                            FILENAME_ANALYZER);

    GList *list = g_hash_table_get_keys(problem_data);
    list = g_list_sort(list, (GCompareFunc)strcmp);
    GList *l;

    /* Print one-liners. Format:
     * NAME1: <maybe more spaces>VALUE1
     * NAME2: <maybe more spaces>VALUE2
     */
    bool empty = true;
    l = list;
    while (l)
    {
        const char *key = l->data;
        l = l->next;

        /* Skip items we are not interested in */
//TODO: optimize by doing this once, not 3 times:
        if (names_to_skip
            && rejected_name(key, names_to_skip, desc_flags))
            continue;

        struct problem_item *item = g_hash_table_lookup(problem_data, key);
        if (!item)
            continue;

        if ((desc_flags & MAKEDESC_SHOW_ONLY_LIST) && !(item->flags & CD_FLAG_LIST))
            continue;

        if ((item->flags & CD_FLAG_TXT)
         && !strchr(item->content, '\n')
        ) {
            char *formatted = problem_item_format(item);
            char *output = formatted ? formatted : item->content;
            int pad = 16 - (strlen(key) + 2);
            if (pad < 0) pad = 0;
            strbuf_append_strf(buf_dsc, "%s: %*s%s\n", key, pad, "", output);
            empty = false;
            free(formatted);
        }
    }

    if (desc_flags & MAKEDESC_SHOW_URLS)
    {
        const char *reported_to = problem_data_get_content_or_NULL(problem_data, FILENAME_REPORTED_TO);
        if (reported_to != NULL)
        {
            GList *reports = read_entire_reported_to_data(reported_to);

            /* The value part begins on 17th column */
            /*                        0123456789ABCDEF*/
            const char *pad_prefix = "                ";
            char *first_prefix = xasprintf("%s%*s", _("Reported:"), 16 - strlen(_("Reported:")), "");
            const char *prefix     = first_prefix;
            for (GList *iter = reports; iter != NULL; iter = g_list_next(iter))
            {
                const report_result_t *const report = (report_result_t *)iter->data;

                if (report->url == NULL)
                    continue;

                strbuf_append_strf(buf_dsc, "%s%s\n", prefix, report->url);

                if (prefix == first_prefix)
                {   /* Only the first URL is prefixed by 'Reported:' */
                    empty = false;
                    prefix = pad_prefix;
                }
            }
            free(first_prefix);
            g_list_free_full(reports, (GDestroyNotify)free_report_result);
        }
    }

    bool append_empty_line = !empty;
    if (desc_flags & MAKEDESC_SHOW_FILES)
    {
        /* Print file info. Format:
         * <empty line if needed>
         * NAME1: <maybe more spaces>Binary file, NNN bytes
         * NAME2: <maybe more spaces>Text file, NNN bytes
         *
         * In many cases, it is useful to know how big binary files are
         * (for example, helps with diagnosing bug upload problems)
         */
        l = list;
        while (l)
        {
            const char *key = l->data;
            l = l->next;

            /* Skip items we are not interested in */
            if (names_to_skip
                && rejected_name(key, names_to_skip, desc_flags))
                continue;

            struct problem_item *item = g_hash_table_lookup(problem_data, key);
            if (!item)
                continue;

            if ((desc_flags & MAKEDESC_SHOW_ONLY_LIST) && !(item->flags & CD_FLAG_LIST))
                continue;

            if ((item->flags & CD_FLAG_BIN)
             || ((item->flags & CD_FLAG_TXT) && strlen(item->content) > max_text_size)
            ) {
                if (append_empty_line)
                    strbuf_append_char(buf_dsc, '\n');
                append_empty_line = false;

                struct stat statbuf;
                int stat_err = 0;
                if (item->flags & CD_FLAG_BIN)
                    stat_err = stat(item->content, &statbuf);
                else
                    statbuf.st_size = strlen(item->content);

                /* We don't print item->content for CD_FLAG_BIN, as it is
                 * always "/path/to/dump/dir/KEY" - not informative.
                 */
                int pad = 16 - (strlen(key) + 2);
                if (pad < 0) pad = 0;
                strbuf_append_strf(buf_dsc,
                        (!stat_err ? "%s: %*s%s file, %llu bytes\n" : "%s: %*s%s file\n"),
                        key,
                        pad, "",
                        ((item->flags & CD_FLAG_BIN) ? "Binary" : "Text"),
                        (long long)statbuf.st_size
                );
                empty = false;
            }
        }
    }

    if (desc_flags & MAKEDESC_SHOW_MULTILINE)
    {
        /* Print multi-liners. Format:
         * <empty line if needed>
         * NAME:
         * :LINE1
         * :LINE2
         * :LINE3
         */
        l = list;
        while (l)
        {
            const char *key = l->data;
            l = l->next;

            /* Skip items we are not interested in */
            if (names_to_skip
                && rejected_name(key, names_to_skip, desc_flags))
                continue;

            struct problem_item *item = g_hash_table_lookup(problem_data, key);
            if (!item)
                continue;

            if ((desc_flags & MAKEDESC_SHOW_ONLY_LIST) && !(item->flags & CD_FLAG_LIST))
                continue;

            if ((item->flags & CD_FLAG_TXT)
                && (strlen(item->content) <= max_text_size
                    || (!strcmp(analyzer, "Kerneloops") && !strcmp(key, FILENAME_BACKTRACE))))
            {
                char *formatted = problem_item_format(item);
                char *output = make_description_item_multiline(key, formatted ? formatted : item->content);

                if (output)
                {
                    if (!empty)
                        strbuf_append_str(buf_dsc, "\n");

                    strbuf_append_str(buf_dsc, output);
                    empty = false;
                    free(output);
                }

                free(formatted);
            }
        }
    }

    g_list_free(list);

    return strbuf_free_nobuf(buf_dsc);
}

#ifdef UNUSED
char* make_description_mailx(problem_data_t *problem_data)
{
    struct strbuf *buf_dsc = strbuf_new();
    struct strbuf *buf_additional_files = strbuf_new();
    struct strbuf *buf_duphash_file = strbuf_new();
    struct strbuf *buf_common_files = strbuf_new();

    GHashTableIter iter;
    char *name;
    struct problem_item *value;
    g_hash_table_iter_init(&iter, problem_data);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&value))
    {
        if (value->flags & CD_FLAG_TXT)
        {
            if ((strcmp(name, FILENAME_DUPHASH) != 0)
             && (strcmp(name, FILENAME_ARCHITECTURE) != 0)
             && (strcmp(name, FILENAME_KERNEL) != 0)
             && (strcmp(name, FILENAME_PACKAGE) != 0)
            ) {
                strbuf_append_strf(buf_additional_files, "%s\n-----\n%s\n\n", name, value->content);
            }
            else if (strcmp(name, FILENAME_DUPHASH) == 0)
                strbuf_append_strf(buf_duphash_file, "%s\n-----\n%s\n\n", name, value->content);
            else
                strbuf_append_strf(buf_common_files, "%s\n-----\n%s\n\n", name, value->content);
        }
    }

    char *common_files = strbuf_free_nobuf(buf_common_files);
    char *duphash_file = strbuf_free_nobuf(buf_duphash_file);
    char *additional_files = strbuf_free_nobuf(buf_additional_files);

    strbuf_append_strf(buf_dsc, "Duplicate check\n=====\n%s\n\n", duphash_file);
    strbuf_append_strf(buf_dsc, "Common information\n=====\n%s\n\n", common_files);
    strbuf_append_strf(buf_dsc, "Additional information\n=====\n%s\n", additional_files);

    free(common_files);
    free(duphash_file);
    free(additional_files);

    return strbuf_free_nobuf(buf_dsc);
}
#endif

/* Items we don't want to include to bz / logger */
static const char *const blacklisted_items[] = {
    CD_DUMPDIR        ,
    FILENAME_ANALYZER ,
    FILENAME_TYPE     ,
    FILENAME_COREDUMP ,
    FILENAME_HOSTNAME ,
    FILENAME_DUPHASH  ,
    FILENAME_UUID     ,
    FILENAME_COUNT    ,
    FILENAME_TAINTED_SHORT,
    FILENAME_ARCHITECTURE,
    FILENAME_PACKAGE,
    FILENAME_OS_RELEASE,
    FILENAME_OS_INFO,
    FILENAME_COMPONENT,
    FILENAME_REASON,
    NULL
};

/* Items we don't want to include in email */
static const char *const blacklisted_items_mailx[] = {
    CD_DUMPDIR        ,
    FILENAME_ANALYZER ,
    FILENAME_TYPE     ,
    FILENAME_COREDUMP ,
    FILENAME_DUPHASH  ,
    FILENAME_UUID     ,
    FILENAME_COUNT    ,
    FILENAME_TAINTED_SHORT,
    FILENAME_ARCHITECTURE,
    FILENAME_PACKAGE,
    FILENAME_OS_RELEASE,
    FILENAME_COMPONENT,
    FILENAME_REASON,
    NULL
};

char* make_description_bz(problem_data_t *problem_data, unsigned max_text_size)
{
    return make_description(
                problem_data,
                (char**)blacklisted_items,
                max_text_size,
                MAKEDESC_SHOW_FILES | MAKEDESC_SHOW_MULTILINE
    );
}

char* make_description_logger(problem_data_t *problem_data, unsigned max_text_size)
{
    return make_description(
                problem_data,
                (char**)blacklisted_items,
                max_text_size,
                MAKEDESC_SHOW_FILES | MAKEDESC_SHOW_MULTILINE
    );
}

char* make_description_mailx(problem_data_t *problem_data, unsigned max_text_size)
{
    return make_description(
                problem_data,
                (char**)blacklisted_items_mailx,
                max_text_size,
                MAKEDESC_SHOW_FILES | MAKEDESC_SHOW_MULTILINE
    );
}
