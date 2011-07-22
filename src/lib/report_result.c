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
#include "internal_libreport.h"

struct report_result *new_report_result(enum report_result_type type, char *data)
{
    struct report_result *res;
    const char *event;

    if (!(event = getenv("EVENT")))
        event = "unknown";

    res = xmalloc(sizeof (*res));
    res->event = xstrdup(event);
    res->data = data;
    res->type = type;
    res->timestamp = time(NULL);

    return res;
}

char *format_report_result(const struct report_result *result)
{
    const char *type_string;

    switch (result->type)
    {
        case REPORT_RESULT_TYPE_URL:
            type_string = "URL";
            break;
        case REPORT_RESULT_TYPE_MESSAGE:
            type_string = "MSG";
            break;
        default:
            assert(0);
    }
    return xasprintf("%s: TIME=%s %s=%s", result->event,
            iso_date_string(&result->timestamp), type_string, result->data);
}

struct report_result *parse_report_result(const char *text)
{
    struct report_result *res;
    enum report_result_type type;
    time_t ts;
    int event_len, event_ts_len;
    char event[256], timestamp[256], *data;

    if (sscanf(text, "%s%n %s%n", event, &event_len, timestamp, &event_ts_len) != 2)
        return NULL;

    data = skip_whitespace(text + event_ts_len);
    if (!strncmp(data, "URL=", 4))
    {
        data += 4;
        type = REPORT_RESULT_TYPE_URL;
    }
    else if (!strncmp(data, "MSG=", 4))
    {
        data += 4;
        type = REPORT_RESULT_TYPE_MESSAGE;
    }
    else
        return NULL;

    if (strncmp(timestamp, "TIME=", 5))
        return NULL;
    ts = string_iso_date(timestamp + 5);

    res = xmalloc(sizeof (*res));
    res->event = xstrndup(event, event_len - 1);
    res->data = xstrdup(data);
    res->type = type;
    res->timestamp = ts;

    return res;
}

void free_report_result(struct report_result *result)
{
    free(result->event);
    free(result->data);
    free(result);
}
