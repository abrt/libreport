#include <internal_libreport.h>

struct report_result
{
    char *label;
    char *url;
    char *message;
    char *bthash;
    char *workflow;
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

char *report_result_get_workflow(report_result_t *result)
{
    g_return_val_if_fail(NULL != result, NULL);

    return g_strdup(result->workflow);
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

void report_result_set_workflow(report_result_t *result,
                                const char      *workflow)
{
    g_return_if_fail(NULL != result);

    g_clear_pointer(&result->workflow, g_free);

    result->workflow = g_strdup(workflow);
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

    buf = libreport_strbuf_new();

    libreport_strbuf_append_strf(buf, "%s:", result->label);

    if ((time_t)-1 != result->timestamp)
    {
        libreport_strbuf_append_strf(buf, " TIME=%s", libreport_iso_date_string(&result->timestamp));
    }

    if (NULL != result->url)
    {
        libreport_strbuf_append_strf(buf, " URL=%s", result->url);
    }

    if (NULL != result->bthash)
    {
        libreport_strbuf_append_strf(buf, " BTHASH=%s", result->bthash);
    }

    if (NULL != result->workflow)
    {
        libreport_strbuf_append_strf(buf, " WORKFLOW=%s", result->workflow);
    }

    /* MSG must be last because the value is delimited by new line character */
    if (NULL != result->message)
    {
        libreport_strbuf_append_strf(buf, " MSG=%s", result->message);
    }

    return buf;
}

static report_result_t *report_result_new(void)
{
    report_result_t *result;

    result = g_new0(report_result_t, 1);

    result->timestamp = (time_t)-1;

    return result;
}

report_result_t *report_result_new_with_label(const char *label)
{
    report_result_t *result;

    g_return_val_if_fail(NULL != label, NULL);
    g_return_val_if_fail('\0' != *label, NULL);
    g_return_val_if_fail(strchr(label, ':') == NULL, NULL);

    result = report_result_new();

    result->label = g_strdup(label);

    return result;
}

report_result_t *report_result_new_with_label_from_env(const char *label)
{
    report_result_t *result;
    const char *workflow;

    result = report_result_new_with_label(label);
    workflow = getenv("LIBREPORT_WORKFLOW");

    if (NULL != workflow)
    {
        result->workflow = g_strdup(workflow);
    }

    return result;
}

report_result_t *report_result_parse(const char *line,
                                     size_t      label_length)
{
    report_result_t *result;

    result = report_result_new();

    result->label = libreport_xstrndup(line, label_length);

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

        const char *end = libreport_skip_non_whitespace(line);

        prefix = "MSG=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            /* MSG=... eats entire line: exiting the loop */
            end = strchrnul(end, '\n');
            result->message = libreport_xstrndup(line + prefix_length, end - (line + prefix_length));
            break;
        }

        prefix = "URL=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            result->url = libreport_xstrndup(line + prefix_length, end - (line + prefix_length));
        }

        prefix = "BTHASH=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            result->bthash = libreport_xstrndup(line + prefix_length, end - (line + prefix_length));
        }

        prefix = "WORKFLOW=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            result->workflow = libreport_xstrndup(line + prefix_length, end - (line + prefix_length));
        }

        prefix = "TIME=";
        prefix_length = strlen(prefix);

        if (strncmp(line, prefix, prefix_length) == 0)
        {
            char *datetime;

            datetime = libreport_xstrndup(line + prefix_length, end - (line + prefix_length));

            if (libreport_iso_date_string_parse(datetime, &result->timestamp) != 0)
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
    g_free(result->workflow);

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

    if (g_strcmp0(lhs->workflow, rhs->workflow) != 0)
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
