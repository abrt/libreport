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

#define free_global_configuration libreport_free_global_configuration
void free_global_configuration(void);

#define get_global_always_excluded_elements libreport_get_global_always_excluded_elements
string_vector_ptr_t get_global_always_excluded_elements(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBREPORT_GLOBAL_CONFIGURATION_H */
