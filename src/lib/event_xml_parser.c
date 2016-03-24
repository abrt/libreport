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
#include "event_config.h"
#include "internal_libreport.h"

#define EVENT_ELEMENT           "event"
#define LABEL_ELEMENT           "label"
#define DESCRIPTION_ELEMENT     "description"
#define LONG_DESCR_ELEMENT      "long-description"
#define ALLOW_EMPTY_ELEMENT     "allow-empty"
#define NOTE_HTML_ELEMENT       "note-html"
#define CREATES_ELEMENT         "creates-items"
#define OPTION_ELEMENT          "option"
//#define ACTION_ELEMENT        "action"
#define NAME_ELEMENT            "name"
#define DEFAULT_VALUE_ELEMENT   "default-value"
#define MINIMAL_RATING_ELEMENT  "minimal-rating"
#define GUI_REVIEW_ELEMENTS     "gui-review-elements"
#define SENDING_SENSITIVE_DATA_ELEMENT  "sending-sensitive-data"
#define SUPPORTS_RESTRICTED_ACCESS_ELEMENT "support-restricted-access"
#define RESTRICTED_ACCESS_OPTION_ATTR "optionname"
#define REQUIRES_DETAILS        "requires-details"

#define REQUIRES_ELEMENT        "requires-items"
#define EXCL_BY_DEFAULT_ELEMENT "exclude-items-by-default"
#define INCL_BY_DEFAULT_ELEMENT "include-items-by-default"
#define EXCL_ALWAYS_ELEMENT     "exclude-items-always"
#define EXCL_BINARY_ELEMENT     "exclude-binary-items"
#define ADV_OPTIONS_ELEMENT     "advanced-options"
#define IMPORT_OPTIONS_ELEMENT  "import-event-options"

typedef struct
{
    event_config_t *values;
    bool exact_name;
    bool exact_description;
    bool exact_long_description;
} parsed_event_config_t;

typedef struct
{
    event_option_t *values;
    bool exact_label;
    bool exact_note_html;
} parsed_event_option_t;

struct my_parse_data
{
    parsed_event_config_t event_config;
    parsed_event_option_t cur_option;
    char *cur_locale;
    char *attribute_lang;
    bool in_adv_option;
};

static const char *const option_types[] =
{
    [OPTION_TYPE_TEXT     ] = "text",
    [OPTION_TYPE_BOOL     ] = "bool",
    [OPTION_TYPE_PASSWORD ] = "password",
    [OPTION_TYPE_NUMBER   ] = "number",
    [OPTION_TYPE_HINT_HTML] = "hint-html",
    [OPTION_TYPE_INVALID  ] = NULL
};

// Return xml:lang value for <foo xml:lang="value"> if value matches current locale,
// "" if foo has no xml:lang attribute at all,
// else (if xml:lang is for some other locale) return NULL
//
static char *get_element_lang(struct my_parse_data *parse_data, const gchar **att_names, const gchar **att_values)
{
    char *short_locale_end = strchr(parse_data->cur_locale, '_');
    //log_debug("locale: %s", parse_data->cur_locale);
    int i;
    for (i = 0; att_names[i] != NULL; ++i)
    {
        //log_debug("attr: %s:%s", att_names[i], att_values[i]);
        if (strcmp(att_names[i], "xml:lang") == 0)
        {
            if (strcmp(att_values[i], parse_data->cur_locale) == 0)
            {
                log_debug("found translation for: %s", parse_data->cur_locale);
                return xstrdup(att_values[i]);
            }

            /* try to match shorter locale
             * e.g: "cs" with cs_CZ
            */
            if (short_locale_end
             && strncmp(att_values[i], parse_data->cur_locale, short_locale_end - parse_data->cur_locale) == 0
            ) {
                log_debug("found translation for shortlocale: %s", parse_data->cur_locale);
                return xstrndup(att_values[i], short_locale_end - parse_data->cur_locale);
            }
        }
    }
    /* if the element has no attribute then it's a default non-localized value */
    if (i == 0)
        return xstrdup("");
    /* if the element is in different language than the current locale */
    return NULL;
}

static int cmp_event_option_name_with_string(gconstpointer a, gconstpointer b)
{
    const event_option_t *evopt = a;
    /* "When it is not a match?" */
    return !evopt->eo_name || strcmp(evopt->eo_name, (char *)b) != 0;
}

static void consume_cur_option(struct my_parse_data *parse_data)
{
    event_option_t *opt = parse_data->cur_option.values;
    if (!opt)
        return;
    memset(&parse_data->cur_option, 0, sizeof(parse_data->cur_option));

    parsed_event_config_t *event_config = &parse_data->event_config;

    /* Example of "nameless" option: <option type="hint-html">
     * The remaining code does not like "nameless" options
     * (strcmp would segfault, etc), so provide invented name:
     */
    if (!opt->eo_name)
        opt->eo_name = xasprintf("%u", (unsigned)g_list_length(event_config->values->options));

    GList *elem = g_list_find_custom(event_config->values->options, opt->eo_name, cmp_event_option_name_with_string);
    if (elem)
    {
        /* we already have option with such name */
        event_option_t *old_opt = elem->data;
        if (old_opt->eo_value)
        {
            /* ...and it already has a value, which
             * overrides xml-defined default one:
             */
            free(opt->eo_value);
            opt->eo_value = old_opt->eo_value;
            old_opt->eo_value = NULL;
        }
        //log("xml: replacing '%s' value:'%s'->'%s'", opt->eo_name, old_opt->eo_value, opt->eo_value);
        free_event_option(old_opt);
        elem->data = opt;
    }
    else
    {
        //log("xml: new value %s='%s'", opt->eo_name, opt->eo_value);
        event_config->values->options = g_list_append(event_config->values->options, opt);
    }
}

// Called for opening tags <foo bar="baz">
static void start_element(GMarkupParseContext *context,
                  const gchar *element_name,
                  const gchar **attribute_names,
                  const gchar **attribute_values,
                  gpointer user_data,
                  GError **error)
{
    //log("start: %s", element_name);

    struct my_parse_data *parse_data = user_data;
    if (strcmp(element_name, ADV_OPTIONS_ELEMENT) == 0)
    {
        parse_data->in_adv_option = true;
        return;
    }
    if (strcmp(element_name, OPTION_ELEMENT) == 0)
    {
        if (parse_data->cur_option.values)
        {
            error_msg("error, option nested in option");
            return;
        }

        memset(&parse_data->cur_option, 0, sizeof(parse_data->cur_option));
        event_option_t *opt = parse_data->cur_option.values = new_event_option();
        opt->is_advanced = (parse_data->in_adv_option == true);
        int i;

        for (i = 0; attribute_names[i] != NULL; ++i)
        {
            log_info("attr: %s:%s", attribute_names[i], attribute_values[i]);
            if (strcmp(attribute_names[i], "name") == 0)
            {
                free(opt->eo_name);
                opt->eo_name = xstrdup(attribute_values[i]);
            }
            else if (strcmp(attribute_names[i], "type") == 0)
            {
                option_type_t type;
                for (type = OPTION_TYPE_TEXT; type < OPTION_TYPE_INVALID; ++type)
                {
                    if (strcmp(option_types[type], attribute_values[i]) == 0)
                        opt->eo_type = type;
                }
            }
        }
    }
    else
    if (strcmp(element_name, IMPORT_OPTIONS_ELEMENT) == 0)
    {
        if (attribute_names[0] == NULL
            || attribute_names[1] != NULL
            || strcmp(attribute_names[0], "event") != 0)
        {
            error_msg("XML event configuration error: import-event-options element misses attribute 'event'");
            return;
        }

        GList *head = parse_data->event_config.values->ec_imported_event_names;
        parse_data->event_config.values->ec_imported_event_names = g_list_append(head, xstrdup(attribute_values[0]));
    }
    else
    if (strcmp(element_name, LABEL_ELEMENT) == 0
     || strcmp(element_name, DESCRIPTION_ELEMENT) == 0
     || strcmp(element_name, LONG_DESCR_ELEMENT) == 0
     || strcmp(element_name, NAME_ELEMENT) == 0
     || strcmp(element_name, NOTE_HTML_ELEMENT) == 0
    ) {
        free(parse_data->attribute_lang);
        parse_data->attribute_lang = get_element_lang(parse_data, attribute_names, attribute_values);
    }
    else
    if (strcmp(element_name, SUPPORTS_RESTRICTED_ACCESS_ELEMENT) == 0)
    {
        if ((     attribute_names[0] != NULL
               && strcmp(attribute_names[0], RESTRICTED_ACCESS_OPTION_ATTR) != 0)
            || attribute_names[1] != NULL)
        {
            error_msg("XML event configuration error: '%s' element misses attribute '%s'",
                    SUPPORTS_RESTRICTED_ACCESS_ELEMENT, RESTRICTED_ACCESS_OPTION_ATTR);
            return;
        }

        parse_data->event_config.values->ec_restricted_access_option = xstrdup(attribute_values[0]);
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

    if (strcmp(element_name, ADV_OPTIONS_ELEMENT) == 0)
    {
        parse_data->in_adv_option = false;
    }
    if (strcmp(element_name, OPTION_ELEMENT) == 0)
    {
        consume_cur_option(parse_data);
    }
    if (strcmp(element_name, EVENT_ELEMENT) == 0)
    {
        consume_cur_option(parse_data);
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
    event_config_t *ui = parse_data->event_config.values;

    const gchar *inner_element = g_markup_parse_context_get_element(context);
    char *text_copy = xstrndup(text, text_len);
    event_option_t *opt = parse_data->cur_option.values;
    if (opt)
    {
        if (strcmp(inner_element, LABEL_ELEMENT) == 0)
        {
            if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
            {
                /* set the value only if we found a value for the current locale
                 * OR the label is still not set and we found the default value
                 */
                if (parse_data->attribute_lang[0] != '\0'
                 || !opt->eo_label /* && parse_data->attribute_lang is "" - always true */
                ) {
                    if (!parse_data->cur_option.exact_label)
                    {
                        parse_data->cur_option.exact_label =
                            (strcmp(parse_data->attribute_lang, parse_data->cur_locale) == 0);

                        log_info("new label:'%s'", text_copy);
                        free(opt->eo_label);
                        opt->eo_label = text_copy;
                        text_copy = NULL;
                    }
                }
            }
        }
        /*
         * we can add a separate field for the default value
         * in that case we can implement features like "reset to default value"
         * but for now using "value" should be enough and clients doesn't
         * have to know about the "defaul-value"
         */
        else if (strcmp(inner_element, DEFAULT_VALUE_ELEMENT) == 0)
        {
            log_info("default value:'%s'", text_copy);
            free(opt->eo_value);
            opt->eo_value = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, NOTE_HTML_ELEMENT) == 0)
        {
            if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
            {
                /* set the value only if we found a value for the current locale
                 * OR the label is still not set and we found the default value
                 */
                if (parse_data->attribute_lang[0] != '\0'
                 || !opt->eo_note_html /* && parse_data->attribute_lang is "" - always true */
                ) {
                    if (!parse_data->cur_option.exact_note_html)
                    {
                        parse_data->cur_option.exact_note_html =
                            (strcmp(parse_data->attribute_lang, parse_data->cur_locale) == 0);

                        log_info("html note:'%s'", text_copy);
                        free(opt->eo_note_html);
                        opt->eo_note_html = text_copy;
                        text_copy = NULL;
                    }
                }
            }
        }
        else if (strcmp(inner_element, ALLOW_EMPTY_ELEMENT) == 0)
        {
            log_info("allow-empty:'%s'", text_copy);
            opt->eo_allow_empty = string_to_bool(text_copy);
        }
        /*
        if (strcmp(inner_element, DESCRIPTION_ELEMENT) == 0)
        {
            log_info("tooltip:'%s'", text_copy);
            free(opt->eo_description);
            opt->eo_description = text_copy;
            text_copy = NULL;
        }
        */
    }
    else
    {
        /* we're not in option, so the description is for the event */
        /*
        if (strcmp(inner_element, ACTION_ELEMENT) == 0)
        {
            log_info("action description:'%s'", text_copy);
            free(ui->action);
            ui->action = text_copy;
            text_copy = NULL;
        }
        */
        if (strcmp(inner_element, CREATES_ELEMENT) == 0)
        {
            log_info("ec_creates_items:'%s'", text_copy);
            free(ui->ec_creates_items);
            ui->ec_creates_items = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, NAME_ELEMENT) == 0)
        {
            if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
            {
                /* set the value only if we found a value for the current locale
                 * OR the label is still not set and we found the default value
                 */
                if (parse_data->attribute_lang[0] != '\0'
                 || !ec_get_screen_name(ui) /* && parse_data->attribute_lang is "" - always true */
                ) {
                    if (!parse_data->event_config.exact_name)
                    {
                        parse_data->event_config.exact_name =
                            (strcmp(parse_data->attribute_lang, parse_data->cur_locale) == 0);

                        log_info("event name:'%s'", text_copy);
                        ec_set_screen_name(ui, text_copy);
                        free(text_copy);
                        text_copy = NULL;
                    }
                }
            }
        }
        else if (strcmp(inner_element, DESCRIPTION_ELEMENT) == 0)
        {
            log_debug("event description:'%s'", text_copy);

            if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
            {
                /* set the value only if we found a value for the current locale
                 * OR the description is still not set and we found the default value
                 */
                if (parse_data->attribute_lang[0] != '\0'
                 || !ec_get_description(ui) /* && parse_data->attribute_lang is "" - always true */
                ) {
                    if (!parse_data->event_config.exact_description)
                    {
                        parse_data->event_config.exact_description =
                            (strcmp(parse_data->attribute_lang, parse_data->cur_locale) == 0);

                        ec_set_description(ui, text_copy);
                        free(text_copy);
                        text_copy = NULL;
                    }
                }
            }
        }
        else if (strcmp(inner_element, LONG_DESCR_ELEMENT) == 0)
        {
            log_debug("event long description:'%s'", text_copy);

            if (parse_data->attribute_lang != NULL) /* if it isn't for other locale */
            {
                /* set the value only if we found a value for the current locale
                 * OR the description is still not set and we found the default value
                 */
                if (parse_data->attribute_lang[0] != '\0'
                 || !ec_get_long_desc(ui) /* && parse_data->attribute_lang is "" - always true */
                ) {

                    if (!parse_data->event_config.exact_long_description)
                    {
                        parse_data->event_config.exact_long_description =
                            (strcmp(parse_data->attribute_lang, parse_data->cur_locale) == 0);

                        ec_set_long_desc(ui, text_copy);
                        free(text_copy);
                        text_copy = NULL;
                    }
                }
            }
        }
        else if (strcmp(inner_element, REQUIRES_ELEMENT) == 0)
        {
            free(ui->ec_requires_items);
            ui->ec_requires_items = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, EXCL_BY_DEFAULT_ELEMENT) == 0)
        {
            free(ui->ec_exclude_items_by_default);
            ui->ec_exclude_items_by_default = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, INCL_BY_DEFAULT_ELEMENT) == 0)
        {
            free(ui->ec_include_items_by_default);
            ui->ec_include_items_by_default = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, EXCL_ALWAYS_ELEMENT) == 0)
        {
            free(ui->ec_exclude_items_always);
            ui->ec_exclude_items_always = text_copy;
            text_copy = NULL;
        }
        else if (strcmp(inner_element, EXCL_BINARY_ELEMENT) == 0)
        {
            ui->ec_exclude_binary_items = string_to_bool(text_copy);
        }
        else if (strcmp(inner_element, MINIMAL_RATING_ELEMENT) == 0)
        {
            char *endptr;
            errno = 0;
            ui->ec_minimal_rating = strtol(text_copy, &endptr, 10);
            if (errno != 0 || endptr == text_copy || *endptr != '\0')
            {
                log("invalid minimal-rating number '%s', set to default 4", text_copy);
                ui->ec_minimal_rating = 4;
            }
        }
        else if (strcmp(inner_element, GUI_REVIEW_ELEMENTS) == 0)
        {
            ui->ec_skip_review = !string_to_bool(text_copy);
        }
        else if (strcmp(inner_element, SENDING_SENSITIVE_DATA_ELEMENT) == 0)
        {
            ui->ec_sending_sensitive_data = string_to_bool(text_copy);
        }
        else if (strcmp(inner_element, SUPPORTS_RESTRICTED_ACCESS_ELEMENT) == 0)
        {
            ui->ec_supports_restricted_access = string_to_bool(text_copy);
        }
        else if (strcmp(inner_element, REQUIRES_DETAILS) == 0)
        {
            ui->ec_requires_details = string_to_bool(text_copy);
        }
    }
    free(text_copy);
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

/* this function takes 2 parameters
 * ui -> pointer to event_config_t
 * filename -> filename to read
 * event_config_t contains list of options, which is malloced by hits function
 * and must be freed by the caller
 */

void load_event_description_from_file(event_config_t *event_config, const char* filename)
{
    log_notice("loading event: '%s'", filename);
    struct my_parse_data parse_data = { {event_config, false, false, false}, {NULL, false, false}, NULL, NULL };
    parse_data.cur_locale = xstrdup(setlocale(LC_ALL, NULL));
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
    else
        perror_msg("cannot load event description from file : '%s'", filename);

    g_markup_parse_context_free(context);

    consume_cur_option(&parse_data); /* just in case */
    free(parse_data.attribute_lang); /* just in case */
    free(parse_data.cur_locale);
}
