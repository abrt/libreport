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
#include "internal_libreport.h"
#include "abrt_xmlrpc.h"
#include "proxies.h"

struct abrt_xmlrpc_param_pair
{
    char *name;
    xmlrpc_value *value;
};

void abrt_xmlrpc_die(xmlrpc_env *env)
{
    error_msg_and_die("fatal: %s", env->fault_string);
}

void abrt_xmlrpc_error(xmlrpc_env *env)
{
    error_msg("error: %s", env->fault_string);
}

struct abrt_xmlrpc *abrt_xmlrpc_new_client(const char *url, int ssl_verify)
{
    GList *proxies;
    xmlrpc_env env;
    xmlrpc_env_init(&env);

    struct abrt_xmlrpc *ax = xzalloc(sizeof(struct abrt_xmlrpc));

    /* This should be done at program startup, once. We do it in main */
    /* xmlrpc_client_setup_global_const(&env); */

    /* URL - #666893, 814628 Unable to make sense of
     * XML-RPC response from server
     *
     * By default, XML data from the network may be no larger than 512K.
     * XMLRPC_XML_SIZE_LIMIT_DEFAULT is #defined to (512*1024) in xmlrpc-c/base.h
     *
     * Users reported trouble with 733402 byte long responses, hope raising the
     * limit to 4*512k is enough.
     * #961520 (2013-05-09): apparently 4*512k is still too small, making it 8*512k.
     */
    xmlrpc_limit_set(XMLRPC_XML_SIZE_LIMIT_ID, 8 * XMLRPC_XML_SIZE_LIMIT_DEFAULT);

    struct xmlrpc_curl_xportparms curl_parms;
    memset(&curl_parms, 0, sizeof(curl_parms));
    /* curlParms.network_interface = NULL; - done by memset */
    curl_parms.no_ssl_verifypeer = !ssl_verify;
    curl_parms.no_ssl_verifyhost = !ssl_verify;
#ifdef VERSION
    curl_parms.user_agent        = PACKAGE_NAME"/"VERSION;
#else
    curl_parms.user_agent        = "abrt";
#endif

    proxies = get_proxy_list(url);
    /* Use the first proxy from the list */
    if (proxies)
        curl_parms.proxy = (const char *)proxies->data;

    struct xmlrpc_clientparms client_parms;
    memset(&client_parms, 0, sizeof(client_parms));
    client_parms.transport          = "curl";
    client_parms.transportparmsP    = &curl_parms;
    client_parms.transportparm_size = XMLRPC_CXPSIZE(proxy);

    xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS,
                         PACKAGE_NAME, VERSION,
                         &client_parms, XMLRPC_CPSIZE(transportparm_size),
                         &ax->ax_client);

    list_free_with_free(proxies);

    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    ax->ax_server_info = xmlrpc_server_info_new(&env, url);
    if (env.fault_occurred)
    {
        /* xmlrpc_client_destroy(ax->ax_client); */
        abrt_xmlrpc_die(&env);
    }

    return ax;
}

void abrt_xmlrpc_free_client(struct abrt_xmlrpc *ax)
{
    if (!ax)
        return;

    if (ax->ax_server_info)
        xmlrpc_server_info_free(ax->ax_server_info);

    if (ax->ax_client)
        xmlrpc_client_destroy(ax->ax_client);

    for (GList *iter = ax->ax_session_params; iter; iter = g_list_next(iter))
    {
        struct abrt_xmlrpc_param_pair *param_pair = (struct abrt_xmlrpc_param_pair *)iter->data;
        xmlrpc_DECREF(param_pair->value);
        free(param_pair->name);
        free(param_pair);
    }

    g_list_free(ax->ax_session_params);

    free(ax);
}

void abrt_xmlrpc_client_add_session_param_string(xmlrpc_env *env, struct abrt_xmlrpc *ax,
        const char *name, const char *value)
{
    struct abrt_xmlrpc_param_pair *new_ses_param = xmalloc(sizeof(*new_ses_param));
    new_ses_param->name = xstrdup(name);

    new_ses_param->value = xmlrpc_string_new(env, value);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    ax->ax_session_params = g_list_append(ax->ax_session_params, new_ses_param);
}

/* internal helper function */
static xmlrpc_value *abrt_xmlrpc_call_params_internal(xmlrpc_env *env, struct abrt_xmlrpc *ax, const char *method, xmlrpc_value *params)
{
    xmlrpc_value *array = xmlrpc_array_new(env);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    bool destroy_params = false;
    if (xmlrpc_value_type(params) == XMLRPC_TYPE_NIL)
    {
        destroy_params = true;
        params = abrt_xmlrpc_params_new(env);
    }

    if (xmlrpc_value_type(params) == XMLRPC_TYPE_STRUCT)
    {
        for (GList *iter = ax->ax_session_params; iter; iter = g_list_next(iter))
        {
            struct abrt_xmlrpc_param_pair *param_pair = (struct abrt_xmlrpc_param_pair *)iter->data;

            xmlrpc_struct_set_value(env, params, param_pair->name, param_pair->value);
            if (env->fault_occurred)
                abrt_xmlrpc_die(env);
        }
    }
    else
    {
        log("Bug: not yet supported XML RPC call type.");
    }

    xmlrpc_array_append_item(env, array, params);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_value *result = NULL;
    xmlrpc_client_call2(env, ax->ax_client, ax->ax_server_info, method,
                        array, &result);

    if (destroy_params)
        xmlrpc_DECREF(params);

    xmlrpc_DECREF(array);
    return result;
}

/* internal helper function */
static
xmlrpc_value *abrt_xmlrpc_call_full_va(xmlrpc_env *env, struct abrt_xmlrpc *ax,
                                       const char *method, const char *format,
                                       va_list args)
{
    xmlrpc_env_init(env);

    xmlrpc_value* param = NULL;
    const char* suffix;

    xmlrpc_build_value_va(env, format, args, &param, &suffix);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_value *result = NULL;
    if (*suffix != '\0')
    {
        xmlrpc_env_set_fault_formatted(
            env, XMLRPC_INTERNAL_ERROR, "Junk after the argument "
            "specifier: '%s'.  There must be exactly one argument.",
            suffix);
    }
    else
        result = abrt_xmlrpc_call_params_internal(env, ax, method, param);

    xmlrpc_DECREF(param);

    return result;
}

xmlrpc_value *abrt_xmlrpc_array_new(xmlrpc_env *env)
{
    xmlrpc_value *params = xmlrpc_array_new(env);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    return params;
}

void abrt_xmlrpc_array_append_string(xmlrpc_env *env, xmlrpc_value *array, const char *value)
{
    xmlrpc_value *val = xmlrpc_string_new(env, value);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_array_append_item(env, array, val);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_DECREF(val);
}

xmlrpc_value *abrt_xmlrpc_params_new(xmlrpc_env *env)
{
    xmlrpc_value *params = xmlrpc_struct_new(env);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    return params;
}

void abrt_xmlrpc_params_add_string(xmlrpc_env *env, xmlrpc_value *params, const char *name, const char *value)
{
    xmlrpc_value *val = xmlrpc_string_new(env, value);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_struct_set_value(env, params, name, val);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_DECREF(val);
}

void abrt_xmlrpc_params_add_int(xmlrpc_env *env, xmlrpc_value *params, const char *name, int value)
{
    xmlrpc_value *val = xmlrpc_int_new(env, value);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_struct_set_value(env, params, name, val);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    xmlrpc_DECREF(val);
}

void abrt_xmlrpc_params_add_array(xmlrpc_env *env, xmlrpc_value *params, const char *name, xmlrpc_value *value)
{
    xmlrpc_struct_set_value(env, params, name, value);
    if (env->fault_occurred)
        abrt_xmlrpc_die(env);
}

xmlrpc_value *abrt_xmlrpc_call_params(xmlrpc_env *env, struct abrt_xmlrpc *ax, const char *method, xmlrpc_value *params)
{
    xmlrpc_value *result = abrt_xmlrpc_call_params_internal(env, ax, method, params);

    if (env->fault_occurred)
        abrt_xmlrpc_die(env);

    return result;
}

xmlrpc_value *abrt_xmlrpc_call_full(xmlrpc_env *env, struct abrt_xmlrpc *ax,
                                    const char *method, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    xmlrpc_value *result = abrt_xmlrpc_call_full_va(env, ax, method, format, args);
    va_end(args);

    return result;
}

/* die or return expected results */
xmlrpc_value *abrt_xmlrpc_call(struct abrt_xmlrpc *ax,
                               const char *method, const char *format, ...)
{
    xmlrpc_env env;

    va_list args;
    va_start(args, format);
    xmlrpc_value *result = abrt_xmlrpc_call_full_va(&env, ax, method, format, args);
    va_end(args);

    if (env.fault_occurred)
        abrt_xmlrpc_die(&env);

    return result;
}
