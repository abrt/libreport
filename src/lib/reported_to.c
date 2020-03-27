/*
    Copyright (C) 2013  ABRT Team
    Copyright (C) 2013  RedHat inc.

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
#include "dump_dir.h"
#include "internal_libreport.h"

int libreport_add_reported_to_data(char **reported_to, const char *line)
{
    if (*reported_to)
    {
        unsigned len_line = strlen(line);
        char *p = *reported_to;
        while (*p)
        {
            if (strncmp(p, line, len_line) == 0 && (p[len_line] == '\n' || p[len_line] == '\0'))
                return 0;
            p = strchrnul(p, '\n');
            if (!*p)
                break;
            p++;
        }
        if (p != *reported_to && p[-1] != '\n')
            *reported_to = append_to_malloced_string(*reported_to, "\n");
        *reported_to = append_to_malloced_string(*reported_to, line);
        *reported_to = append_to_malloced_string(*reported_to, "\n");
    }
    else
        *reported_to = xasprintf("%s\n", line);

    return 1;
}

int libreport_add_reported_to_entry_data(char **reported_to, report_result_t *result)
{
    struct strbuf *buf;

    buf = report_result_to_string(result);
    if (NULL == buf)
    {
        return -EINVAL;
    }

    const int r = libreport_add_reported_to_data(reported_to, buf->buf);

    strbuf_free(buf);

    return r;
}

typedef void (* foreach_reported_to_line_cb_type)(const char *record_line, size_t label_len, void *user_data);

static void foreach_reported_to_line(const char *reported_to, foreach_reported_to_line_cb_type callback, void *user_data)
{
    const char *p = reported_to;
    unsigned lineno = 0;
    while (*p)
    {
        ++lineno;

        const char *record = p;
        const char *record_label_end = strchrnul(p, ':');
        const size_t label_len = record_label_end - p;
        const char *record_end = strchrnul(p, '\n');

        p = record_end + (record_end[0] != '\0');

        if (label_len == 0 || record_label_end[0] == '\0' || record_end < record_label_end)
        {
            log_notice("Miss formatted 'reported_to' record on line %d", lineno);
            continue;
        }

        callback(record, label_len, user_data);
    }
}

static void read_entire_reported_to_cb(const char *record_line, size_t label_len, void *user_data)
{
    GList **result = (GList **)user_data;
    report_result_t *report = report_result_parse(record_line, label_len);
    *result = g_list_prepend(*result, report);
}

GList *libreport_read_entire_reported_to_data(const char *reported_to)
{
    GList *result = NULL;
    foreach_reported_to_line(reported_to, read_entire_reported_to_cb, &result);
    return g_list_reverse(result);
}

struct find_in_cb_data
{
    const char *label;
    size_t label_len;
    const char *found;
    size_t found_label_len;
};

static void find_in_reported_to_cb(const char *record_line, size_t label_len, void *user_data)
{
    struct find_in_cb_data *search_args = (struct find_in_cb_data *)user_data;
    if (label_len == search_args->label_len && strncmp(search_args->label, record_line, label_len) == 0)
    {
        search_args->found = record_line;
        search_args->found_label_len = label_len;
    }
}

report_result_t *libreport_find_in_reported_to_data(const char *reported_to, const char *report_label)
{
    struct find_in_cb_data searched;
    searched.label = report_label;
    searched.label_len = strlen(report_label);
    searched.found = NULL;
    searched.found_label_len = 0;

    foreach_reported_to_line(reported_to, find_in_reported_to_cb, &searched);

    report_result_t *result = NULL;
    if (searched.found)
        result = report_result_parse(searched.found, searched.found_label_len);

    return result;
}
