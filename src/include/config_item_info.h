/*
    Copyright (C) 2011  ABRT team
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

#ifndef LIBREPORT_CONFIG_ITEM_H
#define LIBREPORT_CONFIG_ITEM_H

typedef struct
{
    char *name;     //the event name (from it's filename)
    char *screen_name; //ui friendly name of the event: "Bugzilla" "RedHat Support Upload"
    char *description; // "Report to..."/"Save to file". Should be one sentence, not long
    char *long_desc;  // Long(er) explanation, if needed

} config_item_info_t;

config_item_info_t *new_config_info();
void free_config_info(config_item_info_t *info);

void ci_set_screen_name(config_item_info_t *ci, const char *screen_name);
void ci_set_name(config_item_info_t *ci, const char *name);
void ci_set_description(config_item_info_t *ci, const char *description);
void ci_set_long_desc(config_item_info_t *ci, const char *long_description);

extern const char *ci_get_screen_name(config_item_info_t *ci);
extern const char *ci_get_name(config_item_info_t *ci);
extern const char *ci_get_description(config_item_info_t *ci);
extern const char *ci_get_long_desc(config_item_info_t *ci);

#endif
