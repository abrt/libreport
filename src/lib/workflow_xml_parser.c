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
#include "workflow.h"
#include "internal_libreport.h"
#include "xml_parser.h"

//workflow elements
#define WORKFLOW_ELEMENT        "workflow"
#define EVENTS_ELEMENT          "events"
#define EVENT_ELEMENT           "event"
#define DESCRIPTION_ELEMENT     "description"
#define NAME_ELEMENT            "name"
#define PRIORITY_ELEMENT        "priority"

static void start_element(GMarkupParseContext *context,
                  const gchar *element_name,
                  const gchar **attribute_names,
                  const gchar **attribute_values,
                  gpointer user_data,
                  GError **error)
{
    //log_warning("start: %s", element_name);

    struct my_parse_data *parse_data = user_data;
    if (strcmp(element_name, EVENTS_ELEMENT) == 0)
    {
        parse_data->in_event_list = true;
    }

    if (strcmp(element_name, NAME_ELEMENT) == 0
     || strcmp(element_name, DESCRIPTION_ELEMENT) == 0
    ) {
        free(parse_data->attribute_lang);
        parse_data->attribute_lang = get_element_lang(parse_data, attribute_names, attribute_values);
    }
}

// Called for close tags </foo>
static void end_element(GMarkupParseContext *context,
                          const gchar         *element_name,
                          gpointer             user_data,
                          GError             **error)
{
    struct my_parse_data *parse_data = user_data;

    free(parse_data->attribute_lang);
    parse_data->attribute_lang = NULL;

    if (strcmp(element_name, EVENTS_ELEMENT) == 0)
    {
        parse_data->in_event_list = false;
    }
}

// Called for character data
// text is not nul-terminated
static void text(GMarkupParseContext *context,
         const gchar         *text,
         gsize                text_len,
         gpointer             user_data,
         GError             **error)
{
    struct my_parse_data *parse_data = user_data;
    workflow_t *workflow = parse_data->workflow;

    const gchar *inner_element = g_markup_parse_context_get_element(context);

    if(parse_data->in_event_list && strcmp(inner_element, EVENT_ELEMENT) == 0)
    {
        log_debug("going to expand event name wildcard");
        GList *expanded_events = expand_event_wildcard(text, text_len);

        while (expanded_events)
        {
            const gchar *event_name = expanded_events->data;

            event_config_t *ec = new_event_config(event_name);
            g_autofree gchar *event_file = libreport_xasprintf(EVENTS_DIR"/%s.xml", event_name);

            load_event_description_from_file(ec, event_file);
            if (ec_get_screen_name(ec))
            {
                log_debug("adding event '%s' to workflow", event_name);
                wf_add_event(workflow, ec);
            }
            else
                free_event_config(ec);

            expanded_events = g_list_delete_link(expanded_events, expanded_events);
        }
    }

    if(strcmp(inner_element, NAME_ELEMENT) == 0)
    {
        log_debug("workflow name:'%s'", text);

        if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
        {
            /* set the value only if we found a value for the current locale
             * OR the description is still not set and we found the default value
             */
            if (parse_data->attribute_lang[0] != '\0'
             || !wf_get_screen_name(workflow) /* && parse_data->attribute_lang is "" - always true */
            ) {
                if (!parse_data->exact_name)
                {
                    parse_data->exact_name = (strcmp(parse_data->cur_locale, parse_data->attribute_lang) == 0);
                    wf_set_screen_name(workflow, text);
                }
            }
        }
    }

    else if(strcmp(inner_element, DESCRIPTION_ELEMENT) == 0)
    {
       log_debug("workflow description:'%s'", text);

        if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
        {
            /* set the value only if we found a value for the current locale
             * OR the description is still not set and we found the default value
             */
            if (parse_data->attribute_lang[0] != '\0'
             || !wf_get_description(workflow) /* && parse_data->attribute_lang is "" - always true */
            ) {
                if (!parse_data->exact_description)
                {
                    parse_data->exact_description = (strcmp(parse_data->cur_locale, parse_data->attribute_lang) == 0);
                    wf_set_description(workflow, text);
                }
            }
        }
    }

    else if(strcmp(inner_element, PRIORITY_ELEMENT) == 0)
    {
        log_debug("workflow priority:'%s'", text);

        char *end = NULL;
        long long val = strtoll(text, &end, 10);

        if (text == end || end[0] != '\0'
            || (errno == ERANGE && (val == LLONG_MAX || val == LLONG_MIN))
            || (val > INT_MAX || val < INT_MIN)
            || (errno != 0 && val == 0))
        {
            error_msg("Workflow's priority is not a number in range <%d,%d>", INT_MIN, INT_MAX);
            return;
        }

        wf_set_priority(workflow, (int)val);
    }
}

  // Called for strings that should be re-saved verbatim in this same
  // position, but are not otherwise interpretable.  At the moment
  // this includes comments and processing instructions.
  // text is not nul-terminated
static void passthrough(GMarkupParseContext *context,
                const gchar *passthrough_text,
                gsize text_len,
                gpointer user_data,
                GError **error)
{
    log_debug("passthrough");
}

// Called on error, including one set by other
// methods in the vtable. The GError should not be freed.
static void error(GMarkupParseContext *context,
          GError *error,
          gpointer user_data)
{
    error_msg("error in XML parsing");
}

void load_workflow_description_from_file(workflow_t *workflow, const char* filename)
{
    log_info("loading workflow: '%s'", filename);
    struct my_parse_data parse_data = { workflow, NULL, NULL, 0, 0, 0};
    parse_data.cur_locale = libreport_xstrdup(setlocale(LC_ALL, NULL));
    strchrnul(parse_data.cur_locale, '.')[0] = '\0';

    GMarkupParser parser;
    memset(&parser, 0, sizeof(parser)); /* just in case */
    parser.start_element = &start_element;
    parser.end_element = &end_element;
    parser.text = &text;
    parser.passthrough = &passthrough;
    parser.error = &error;

    GMarkupParseContext *context = g_markup_parse_context_new(
                    &parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                    &parse_data, /*GDestroyNotify:*/ NULL);

    FILE* fin = fopen(filename, "r");
    if (fin != NULL)
    {
        size_t read_bytes;
        char buff[1024];
        while ((read_bytes = fread(buff, 1, 1024, fin)) != 0)
        {
            g_markup_parse_context_parse(context, buff, read_bytes, NULL);
        }
        fclose(fin);
    }

    g_markup_parse_context_free(context);

    free(parse_data.attribute_lang); /* just in case */
    free(parse_data.cur_locale);
}
