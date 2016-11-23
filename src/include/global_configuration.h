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
bool load_global_configuration(void);

#define load_global_configuration_from_dirs libreport_load_global_configuration_from_dirs
bool load_global_configuration_from_dirs(const char *dirs[], int dir_flags[]);

#define free_global_configuration libreport_free_global_configuration
void free_global_configuration(void);

#define get_global_always_excluded_elements libreport_get_global_always_excluded_elements
string_vector_ptr_t get_global_always_excluded_elements(void);

#define get_global_create_private_ticket libreport_get_global_create_private_ticket
bool get_global_create_private_ticket(void);

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
void set_global_create_private_ticket(bool enabled, int flags);

/**
 * Returns logical true if the reporting process shall not start or contine if
 * the not-reportable files exists.
 *
 * The option can be enabled by ABRT_STOP_ON_NOT_REPORTABLE environment
 * variable.
 *
 * @return true if the process shall stop; otherwise the function returns
 * false.
 */
#define get_global_stop_on_not_reportable libreport_get_global_stop_on_not_reportable
bool get_global_stop_on_not_reportable(void);

/**
 * Configures the stop on not reportable global option
 *
 * The function changes the configuration only for the current process by
 * default.
 *
 * The option can be enabled by ABRT_STOP_ON_NOT_REPORTABLE environment
 * variable.
 *
 * @param enabled The option's value
 * @param flags For future needs (enable persistent configuration)
 */
#define set_global_stop_on_not_reportable libreport_set_global_stop_on_not_reportable
void set_global_stop_on_not_reportable(bool enabled, int flags);

#ifdef __cplusplus
}
#endif

#endif /* LIBREPORT_GLOBAL_CONFIGURATION_H */
