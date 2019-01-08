#include <internal_libreport.h>

struct report_result
{
    char *label;
    char *url;
    char *message;
    char *bthash;
    time_t timestamp;
};

char *report_result_get_label(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, NULL);

    return g_strdup(result->label);
}

char *report_result_get_url(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, NULL);

    return g_strdup(result->url);
}

char *report_result_get_message(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, NULL);

    return g_strdup(result->message);
}

char *report_result_get_bthash(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, NULL);

    return g_strdup(result->bthash);
}

time_t report_result_get_timestamp(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, (time_t)-1);

    return result->timestamp;
}

void report_result_set_url(report_result_t *result,
                           const char      *url)
{
    g_return_if_fail(NULL != result);

    g_clear_pointer(&result->url, g_free);

    result->url = g_strdup(url);
}

void report_result_set_message(report_result_t *result,
                               const char      *message)
{
    g_return_if_fail(NULL != result);

    g_clear_pointer(&result->message, g_free);

    result->message = g_strdup(message);
}

void report_result_set_bthash(report_result_t *result,
                              const char      *bthash)
{
    g_return_if_fail(NULL != result);

    g_clear_pointer(&result->bthash, g_free);

    result->bthash = g_strdup(bthash);
}

void report_result_set_timestamp(report_result_t *result,
                                 time_t           timestamp)
{
    g_return_if_fail(NULL != result);

    result->timestamp = timestamp;
}

struct strbuf *report_result_to_string(report_result_t *result)
{
    struct strbuf *buf;

    g_return_val_if_fail(NULL != result, NULL);
    g_return_val_if_fail(NULL != result->label, NULL);

    buf = strbuf_new();

    strbuf_append_strf(buf, "%s:", result->label);

    if ((time_t)-1 != result->timestamp)
    {
        strbuf_append_strf(buf, " TIME=%s", iso_date_string(&result->timestamp));
    }

    if (NULL != result->url)
    {
        strbuf_append_strf(buf, " URL=%s", result->url);
    }

    if (NULL != result->bthash)
    {
        strbuf_append_strf(buf, " BTHASH=%s", result->bthash);
    }

    /* MSG must be last because the value is delimited by new line character */
    if (NULL != result->message)
    {
        strbuf_append_strf(buf, " MSG=%s", result->message);
    }

    return buf;
}

report_result_t *report_result_new(const char *label)
{
    report_result_t *result;

    g_return_val_if_fail(NULL != label, NULL);
    g_return_val_if_fail('\0' != *label, NULL);
    g_return_val_if_fail(strchr(label, ':') == NULL, NULL);

    result = g_new0(report_result_t, 1);

    result->label = g_strdup(label);
    result->timestamp = (time_t)-1;

    return result;
}

report_result_t *report_result_new_parse(const char *line,
                                         size_t      label_length)
{
    report_result_t *result;

    result = g_new0(report_result_t, 1);

    result->label = xstrndup(line, label_length);
    result->timestamp = (time_t)-1;

    /* +1 -> : */
    line += (label_length + 1);

    for (;;)
    {
        const char *prefix;
        size_t prefix_length;

        for(;;)
        {
            if ('\0' == *line || '\n' == *line)
            {
                return result;
            }

            if (!isspace(*line))
            {
                break;
            }

            ++line;
        }

        const char *end = skip_non_whitespace(line);

        prefix = "MSG=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            /* MSG=... eats entire line: exiting the loop */
            end = strchrnul(end, '\n');
            result->message = xstrndup(line + prefix_length, end - (line + prefix_length));
            break;
        }

        prefix = "URL=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            result->url = xstrndup(line + prefix_length, end - (line + prefix_length));
        }

        prefix = "BTHASH=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            result->bthash = xstrndup(line + prefix_length, end - (line + prefix_length));
        }

        prefix = "TIME=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            char *datetime;

            datetime = xstrndup(line + prefix_length, end - (line + prefix_length));

            if (iso_date_string_parse(datetime, &result->timestamp) != 0)
            {
                log_warning(_("Ignored invalid ISO date of report result '%s'"), result->label);
            }

            free(datetime);
        }

        line = end;
    }

    return result;
}

void report_result_free(report_result_t *result)
{
    g_return_if_fail(NULL != result);

    g_free(result->label);
    g_free(result->url);
    g_free(result->message);
    g_free(result->bthash);

    g_free(result);
}

/* Test utilities */

#ifdef LIBREPORT_TEST_REPORT_RESULT

static bool report_result_equals(report_result_t *lhs,
                                 report_result_t *rhs)
{
    if (g_strcmp0(lhs->label, rhs->label) != 0)
    {
        g_return_val_if_reached(false);
    }

    if (g_strcmp0(lhs->url, rhs->url) != 0)
    {
        g_return_val_if_reached(false);
    }

    if (g_strcmp0(lhs->message, rhs->message) != 0)
    {
        g_return_val_if_reached(false);
    }

    if (g_strcmp0(lhs->bthash, rhs->bthash) != 0)
    {
        g_return_val_if_reached(false);
    }

    if (lhs->timestamp != rhs->timestamp)
    {
        g_return_val_if_reached(false);
    }

    return true;
}

#endif
