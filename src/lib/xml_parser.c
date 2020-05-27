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

#include "internal_libreport.h"
#include "xml_parser.h"

char *get_element_lang(struct my_parse_data *parse_data, const gchar **att_names, const gchar **att_values)
{
    /* if the element has no attribute then it's a default non-localized value */
    if (att_values[0] == NULL)
        return g_strdup("");

    char *short_locale_end = strchr(parse_data->cur_locale, '_');
    for (int i = 0; att_names[i] != NULL; ++i)
    {
        if (strcmp(att_names[i], "xml:lang") == 0)
        {
            if (strcmp(att_values[i], parse_data->cur_locale) == 0)
            {
                log_debug("found translation for: %s", parse_data->cur_locale);
                return g_strdup(att_values[i]);
            }

            /* try to match shorter locale
             * e.g: "cs" with cs_CZ
            */
            if (short_locale_end
             && strncmp(att_values[i], parse_data->cur_locale, short_locale_end - parse_data->cur_locale) == 0
            ) {
                log_debug("found translation for shortlocale: %s", parse_data->cur_locale);
                return g_strndup(att_values[i], short_locale_end - parse_data->cur_locale);
            }
        }
    }

    /* if the element is in different language than the current locale */
    return NULL;
}
