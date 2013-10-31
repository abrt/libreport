/*
    Copyright (C) 2013  ABRT team
    Copyright (C) 2013  RedHat Inc

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

/** @file xml_parser.h */

struct my_parse_data
{
    workflow_t *workflow;
    const char *cur_locale;
    char *attribute_lang;
    bool in_event_list;
    bool exact_name;
    bool exact_description;
};

/**
 @brief Gets the language info for the current xml element

 @param parse_data Data parsed from the current element
 @param att_names Names of attributes
 @param att_values Attribute's values

 @return lang value for xml:lang attribute if value matches current locale
 @return "" (empty string) if element has no xml:lang attribute
 @return NULL if the lang value does not match the current locale
*/
char *get_element_lang(struct my_parse_data *parse_data, const gchar **att_names, const gchar **att_values);
