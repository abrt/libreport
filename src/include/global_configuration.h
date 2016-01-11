/*
    Copyright (C) 2015  ABRT team
    Copyright (C) 2015  RedHat Inc

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

#ifndef LIBREPORT_GLOBAL_CONFIGURATION_H
#define LIBREPORT_GLOBAL_CONFIGURATION_H

#include "libreport_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define load_global_configuration libreport_load_global_configuration
int load_global_configuration(void);

#define load_global_configuration_from_dirs libreport_load_global_configuration_from_dirs
int load_global_configuration_from_dirs(const char *dirs[], int dir_flags[]);

#define free_global_configuration libreport_free_global_configuration
void free_global_configuration(void);

#define get_global_always_excluded_elements libreport_get_global_always_excluded_elements
string_vector_ptr_t get_global_always_excluded_elements(void);

#define get_global_create_private_ticket libreport_get_global_create_private_ticket
int get_global_create_private_ticket(void);

/**
 * Configures the create private ticket global option
 *
 * The function changes the configuration only for the current process by
 * default.
 *
 * @param enabled The option's value
 * @param flags For future needs (enable persistent configuration)
 */
#define set_global_create_private_ticket libreport_set_global_create_private_ticket
void set_global_create_private_ticket(int enabled, int flags);

#ifdef __cplusplus
}
#endif

#endif /* LIBREPORT_GLOBAL_CONFIGURATION_H */
