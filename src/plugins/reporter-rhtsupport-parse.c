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
#include "reporter-rhtsupport.h"

struct my_parse_data
{
    int type;
    char *uri;
    char *txt;
    GList *hints_uri;
    GList *hints_txt;
    GList *erratas_uri;
    GList *erratas_txt;
};

// Called for opening tags <foo bar="baz">
static void start_element(
                GMarkupParseContext *context,
                const gchar         *element_name,
                const gchar         **attribute_names,
                const gchar         **attribute_values,
                gpointer            user_data,
                GError              **error)
{
    //log("start: %s", element_name);

    struct my_parse_data *parse_data = user_data;

    if (strcmp(element_name, "link") == 0)
    {
        const char *uri = NULL;
        int type = 0;
        for (int i = 0; attribute_names[i] != NULL; ++i)
        {
            VERB2 log("attr: %s:%s", attribute_names[i], attribute_values[i]);
            if (strcmp(attribute_names[i], "uri") == 0)
            {
                uri = attribute_values[i];
            }
            else if (strcmp(attribute_names[i], "rel") == 0)
            {
                if (strcmp(attribute_values[i], "suggestion") == 0)
                    type = 1;
                else if (strncmp(attribute_values[i], "errata", 6) == 0)
                    type = 2;
            }
        }
        if (uri && type)
        {
            free(parse_data->uri); /* paranoia */
            parse_data->uri = xstrdup(uri);
            parse_data->type = type;
        }
    }
}

// Called for character data between opening and closing tags
// text is not nul-terminated
static void text(
                GMarkupParseContext *context,
                const gchar         *text,
                gsize               text_len,
                gpointer            user_data,
                GError              **error)
{
    struct my_parse_data *parse_data = user_data;

    /* if we are inside valid <link> element... */
    if (parse_data->uri && text_len > 0)
    {
        free(parse_data->txt);
        parse_data->txt = xstrndup(text, text_len);
    }
}

// Called for close tags </foo>
static void end_element(
                GMarkupParseContext *context,
                const gchar         *element_name,
                gpointer            user_data,
                GError              **error)
{
    struct my_parse_data *parse_data = user_data;

    /* if we are closing valid <link> element... */
    if (parse_data->uri)
    {
        /* Note that parse_data->txt may be NULL below */
        if (parse_data->type == 1) /* "suggestion"? */
        {
            parse_data->hints_uri = g_list_append(parse_data->hints_uri, parse_data->uri);
            parse_data->hints_txt = g_list_append(parse_data->hints_txt, parse_data->txt);
        }
        else
        {
            parse_data->erratas_uri = g_list_append(parse_data->erratas_uri, parse_data->uri);
            parse_data->erratas_txt = g_list_append(parse_data->erratas_txt, parse_data->txt);
        }
        parse_data->uri = NULL;
        parse_data->txt = NULL;
    }
}

// Called for strings that should be re-saved verbatim in this same
// position, but are not otherwise interpretable.  At the moment
// this includes comments and processing instructions.
// text is not nul-terminated
static void passthrough(
                GMarkupParseContext *context,
                const gchar         *passthrough_text,
                gsize               text_len,
                gpointer            user_data,
                GError              **error)
{
    VERB3 log("passthrough");
}

// Called on error, including one set by other
// methods in the vtable. The GError should not be freed.
static void error(
                GMarkupParseContext *context,
                GError              *error,
                gpointer            user_data)
{
    error_msg("error in XML parsing");
}

static void emit_url_text_pairs_to_strbuf(struct strbuf *result, GList *urllist, GList *txtlist)
{
    const char *prefix = "";
    while (urllist)
    {
        if (txtlist->data)
        {
            /* changed "%s%s:" -> "%s%s :" if the url ends with ':' then it
             * becomes part of the link and makes it invalid
             */
            strbuf_append_strf(result, "%s%s : %s", prefix, urllist->data, txtlist->data);
            free(txtlist->data);
        }
        else
        {
            strbuf_append_strf(result, "%s%s", prefix, urllist->data);
        }
        free(urllist->data);
        prefix = ", ";
        urllist = g_list_delete_link(urllist, urllist);
        txtlist = g_list_delete_link(txtlist, txtlist);
    }
}

char *parse_response_from_RHTS_hint_xml2txt(const char *string)
{
    if (strncmp(string, "<?xml", 5) != 0)
        return xstrdup(string);

    struct my_parse_data parse_data;
    memset(&parse_data, 0, sizeof(parse_data));

    GMarkupParser parser;
    memset(&parser, 0, sizeof(parser)); /* just in case */
    parser.start_element = &start_element;
    parser.end_element = &end_element;
    parser.text = &text;
    parser.passthrough = &passthrough;
    parser.error = &error;

    GMarkupParseContext *context = g_markup_parse_context_new(
                    &parser, G_MARKUP_TREAT_CDATA_AS_TEXT,
                    &parse_data, /*GDestroyNotify:*/ NULL
    );
    g_markup_parse_context_parse(context, string, strlen(string), NULL);
    g_markup_parse_context_free(context);

    free(parse_data.uri); /* just in case */
    free(parse_data.txt); /* just in case */

    if (!parse_data.hints_uri && !parse_data.erratas_uri)
        return NULL;

    struct strbuf *result = strbuf_new();

    if (parse_data.hints_uri)
    {
        strbuf_append_str(result, _("Documentation which might be relevant: "));
        emit_url_text_pairs_to_strbuf(result, parse_data.hints_uri, parse_data.hints_txt);
        strbuf_append_str(result, ". ");
    }
    if (parse_data.erratas_uri)
    {
        if (parse_data.hints_uri)
            strbuf_append_str(result, " ");
        strbuf_append_str(result, _("Updates which possibly help: "));
        emit_url_text_pairs_to_strbuf(result, parse_data.erratas_uri, parse_data.erratas_txt);
        strbuf_append_str(result, ".");
    }

    return strbuf_free_nobuf(result);
}
