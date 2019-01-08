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
    bool r = is_in_string_list(name, (const char *const *)v);
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

static int list_cmp(const char *s1, const char *s2)
{
    static const char *const list_order[] = {
            FILENAME_REASON    ,
            FILENAME_TIME      ,
            FILENAME_CMDLINE   ,
            FILENAME_PACKAGE   ,
            FILENAME_UID       ,
            FILENAME_COUNT     ,
            NULL
    };
    int s1_index = index_of_string_in_list(s1, list_order);
    int s2_index = index_of_string_in_list(s2, list_order);

    if(s1_index < 0 && s2_index < 0)
        return strcmp(s1, s2);

    if(s1_index < 0)
        return 1;

    if(s2_index < 0)
        return -1;

    return s1_index - s2_index;
}

char *make_description(problem_data_t *problem_data, char **names_to_skip,
                       unsigned max_text_size, unsigned desc_flags)
{
    INITIALIZE_LIBREPORT();

    struct strbuf *buf_dsc = strbuf_new();

    const char *type = problem_data_get_content_or_NULL(problem_data,
                                                            FILENAME_TYPE);

    GList *list = g_hash_table_get_keys(problem_data);
    list = g_list_sort(list, (GCompareFunc)list_cmp);
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
            bool done = false;
            if (strcmp(FILENAME_REASON, key) == 0)
            {
                const char *crash_func = problem_data_get_content_or_NULL(problem_data,
                                                                          FILENAME_CRASH_FUNCTION);
                if((done = (bool)crash_func))
                    strbuf_append_strf(buf_dsc, "%s: %*s%s(): %s\n", key, pad, "", crash_func, output);
            }
            else if (strcmp(FILENAME_UID, key) == 0)
            {
                const char *username = problem_data_get_content_or_NULL(problem_data,
                                                                          FILENAME_USERNAME);
                if((done = (bool)username))
                    strbuf_append_strf(buf_dsc, "%s: %*s%s (%s)\n", key, pad, "", output, username);
            }

            if (!done)
                strbuf_append_strf(buf_dsc, "%s: %*s%s\n", key, pad, "", output);

            empty = false;
            free(formatted);
        }
    }

    if (desc_flags & MAKEDESC_SHOW_URLS)
    {
        if (problem_data_get_content_or_NULL(problem_data, FILENAME_NOT_REPORTABLE) != NULL)
            strbuf_append_strf(buf_dsc, "%s%*s%s\n", _("Reported:"), 16 - strlen(_("Reported:")), "" , _("cannot be reported"));
        else
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
                    report_result_t *report = iter->data;
                    char *url;

                    url = report_result_get_url(report);
                    if (url == NULL)
                        continue;

                    strbuf_append_strf(buf_dsc, "%s%s\n", prefix, url);

                    g_free(url);

                    if (prefix == first_prefix)
                    {   /* Only the first URL is prefixed by 'Reported:' */
                        empty = false;
                        prefix = pad_prefix;
                    }
                }
                free(first_prefix);
                g_list_free_full(reports, (GDestroyNotify)report_result_free);
            }
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

                unsigned long size = 0;
                int stat_err = problem_item_get_size(item, &size);

                /* We don't print item->content for CD_FLAG_BIN, as it is
                 * always "/path/to/dump/dir/KEY" - not informative.
                 */
                int pad = 16 - (strlen(key) + 2);
                if (pad < 0) pad = 0;
                strbuf_append_strf(buf_dsc,
                        (!stat_err ? "%s: %*s%s file, %lu bytes\n" : "%s: %*s%s file\n"),
                        key,
                        pad, "",
                        ((item->flags & CD_FLAG_BIN) ? "Binary" : "Text"),
                        size
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
                    || (!strcmp(type, "Kerneloops") && !strcmp(key, FILENAME_BACKTRACE))))
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

char* make_description_logger(problem_data_t *problem_data, unsigned max_text_size)
{
    return make_description(
                problem_data,
                (char**)blacklisted_items,
                max_text_size,
                MAKEDESC_SHOW_FILES | MAKEDESC_SHOW_MULTILINE
    );
}
