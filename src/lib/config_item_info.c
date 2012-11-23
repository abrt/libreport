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

#include "internal_libreport.h"
//#include "config_item_info.h"


config_item_info_t *new_config_info()
{
    config_item_info_t *info = (config_item_info_t *)xzalloc(sizeof(config_item_info_t));
    return info;
}

void free_config_info(config_item_info_t *info)
{
    if (info == NULL)
        return;

    free(info->name);
    free(info->screen_name);
    free(info->description);
    free(info->long_desc);

    free(info);
}

void ci_set_screen_name(config_item_info_t *ci, const char *screen_name)
{
    free(ci->screen_name);
    ci->screen_name = xstrdup(screen_name);
}

void ci_set_name(config_item_info_t *ci, const char *name)
{
    free(ci->name);
    ci->name = xstrdup(name);
}

void ci_set_description(config_item_info_t *ci, const char *description)
{
    free(ci->description);
    ci->description = xstrdup(description);
}

void ci_set_long_desc(config_item_info_t *ci, const char *long_description)
{
    free(ci->long_desc);
    ci->long_desc = xstrdup(long_description);
}

inline const char *ci_get_screen_name(config_item_info_t *ci)
{
    return ci->screen_name;
}

inline const char *ci_get_name(config_item_info_t *ci)
{
    return ci->name;
}

inline const char *ci_get_description(config_item_info_t *ci)
{
    return ci->description;
}

inline const char *ci_get_long_desc(config_item_info_t *ci)
{
    return ci->long_desc;
}
