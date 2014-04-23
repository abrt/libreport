/*
    Copyright (C) 2010  ABRT team
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
#ifndef ABRT_XMLRPC_H_
#define ABRT_XMLRPC_H_ 1

/* include/stdint.h: typedef int int32_t;
 * include/xmlrpc-c/base.h: typedef int32_t xmlrpc_int32;
 */

#include <glib.h>
#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*abrt_xmlrpc_destroy_fn)(void *);

struct abrt_xmlrpc {
    xmlrpc_client *ax_client;
    xmlrpc_server_info *ax_server_info;
    GList *ax_session_params;
};

xmlrpc_value *abrt_xmlrpc_array_new(xmlrpc_env *env);
void abrt_xmlrpc_array_append_string(xmlrpc_env *env, xmlrpc_value *array, const char *value);

xmlrpc_value *abrt_xmlrpc_params_new(xmlrpc_env *env);
void abrt_xmlrpc_params_add_string(xmlrpc_env *env, xmlrpc_value *params, const char *name, const char *value);
void abrt_xmlrpc_params_add_array(xmlrpc_env *env, xmlrpc_value *params, const char *name, xmlrpc_value *value);


struct abrt_xmlrpc *abrt_xmlrpc_new_client(const char *url, int ssl_verify);
void abrt_xmlrpc_free_client(struct abrt_xmlrpc *ax);
void abrt_xmlrpc_client_add_session_param_string(xmlrpc_env *env, struct abrt_xmlrpc *ax, const char *name, const char *value);
void abrt_xmlrpc_die(xmlrpc_env *env) __attribute__((noreturn));
void abrt_xmlrpc_error(xmlrpc_env *env);

/* die or return expected results */
xmlrpc_value *abrt_xmlrpc_call(struct abrt_xmlrpc *ax,
                               const char *method, const char *format, ...);

xmlrpc_value *abrt_xmlrpc_call_params(xmlrpc_env *env, struct abrt_xmlrpc *ax,
                               const char *method, xmlrpc_value *params);

xmlrpc_value *abrt_xmlrpc_call_full(xmlrpc_env *enf, struct abrt_xmlrpc *ax,
                                   const char *method, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
