/*
    Copyright (C) 2012  ABRT Team
    Copyright (C) 2012  RedHat inc.

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
#include "internal_libreport_gtk.h"

#include <gio/gio.h>

#define SECRETS_SERVICE_BUS "org.freedesktop.secrets"
#define SECRETS_NAME_SPACE(interface) "org.freedesktop.Secret."interface

/* label and alias have to be same because of ksecrets */
#define SECRETS_COLLECTION_LABEL SECRETS_COLLECTION_ALIAS
#define SECRETS_COLLECTION_ALIAS "default"

#define SECRETS_SEARCH_ATTRIBUTE "libreportEventConfig"
#define SECRETS_OPTION_VALUE_DELIMITER '='

/* 5s timeout*/
#define SECRETS_CALL_DEFAULT_TIMEOUT 5000

/* 15s until the timeout dialog will appear  */
#define PROMPT_TIMEOUT_SECONDS 15

/* Well known errors for which we have workarounds */
#define NOT_IMPLEMENTED_READ_ALIAS_ERROR "org.freedesktop.DBus.Error.UnknownMethod"
#define INVALID_PROPERTIES_ARGUMENTS_ERROR "org.freedesktop.DBus.Error.InvalidArgs"
#define GNOME_KEYRING_NOT_HAVING_SECRET_ERROR "org.freedesktop.DBus.Error.Failed"

/*
   Data structure:
   ===============
   Secret Service groups data into Collections. Each collection
   has a label and may have alternative names (aliases).
   There is a default collection, named "default".
   Libreport uses it to store our data.

   Collection is a set of Items identified by labels (label is a string).
   Libreport uses event names as labels for items which hold event config data.
   One item holds all configuration items for one event.

   Item has a set of (name,value) pairs called lookup attributes.
   Libreport uses 'libreportEventConfig' attribute for searching. The
   attribute hodls an event name.
   Item has a secret value. The value holds an arbitrary binary data.
   Libreport uses the value to store configuration items.
   The value consists from several c-strings where each option is
   represented by one c-string (bytes followed by '\0').
   Each option string is concatenation of option name, delimiter and option value.


   For example, "report_Bugzilla" item will have the lookup attribute
   like ("libreportEventConfig", "report_Bugzilla") and the secret value
   like ("Bugzilla_URL=https://bugzilla.redhat.com\0Bugzilla_Login=foo\0")



   DBus API:
   =========
   Secret Service requires user to first open a Session using OpenSession()
   call over session dbus on "/org/freedesktop/secrets" object,
   "org.freedesktop.Secret.Service" interface.
   This creates a new object on "org.freedesktop.secrets" bus.
   Apparently, this object is not used for anything except final Close() call.

   We obtain a dbus object name for the default collection by calling
   ReadAlias() over session dbus on "/org/freedesktop/secrets" object,
   "org.freedesktop.Secret.Service" interface. This object is accessible
   on session dbus. It has "org.freedesktop.Secret.Collection" interface.

   Before we can search for items on the collection, we need to unlock collection
   (this usually causes a "gimme pwd NOW!" prompt to appear if we are in X).
   This is done by Unlock() call. It returns a path to prompt dbus object
   if unlocking is truly needed. In which case we need to call Prompt() on it,
   and wait for dbus signal "Completed" from the prompt object.

   We can SearchItems() and CreateItem() on the default collection.
   SearchItems() returns the list of dbus object names of found items.
   They are accessible on session dbus and have "org.freedesktop.Secret.Item"
   interface. (We use the first found item, if there are more than one.)

   We retrieve attributes from a found item (they are stored as dbus
   object's properties).

   CreateItem() creates an item in a collection and returns a tuple consisting
   from a prompt path and a new item path. We pass item's properties (label and
   attributes) as a dictionary. Dictionary keys are strings and values are
   variants. Label is stored under key "org.freedesktop.Secret.Item.Label" and
   attributes are stored under key "org.freedesktop.Secret.Item.Attributes".
   This function may require to perform a prompt. If the prompt path is the
   special value "/" no prompt is required.
*/

enum {
    /* Find and immediately return collection */
    GET_COLLECTION_FLAGS_NONE = 0,
    /* Return unlocked collection or NULL. */
    GET_COLLECTION_UNLOCK     = 1 << 0,
    /* If collection doesn't exist, create a new one return it. */
    GET_COLLECTION_CREATE     = 1 << 1,
};

enum secrets_service_state
{
    /* intial state */
    SBS_INITIAL = 0,
    /* after opening a secrets service session (different than D-Bus session) */
    SBS_CONNECTED,
    /* secrets service is not available do not bother user anymore */
    SBS_UNAVAILABLE,
};

struct secrets_object
{
    GDBusProxy *proxy;
    /* http://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces-properties */
    GDBusProxy *property_proxy;
    const gchar *interface_name;

    /* various data */
    void *tag;
};

/* DBus session bus connection */
static GDBusConnection *g_connection;

/* State of the service */
static enum secrets_service_state g_state;

/* http://standards.freedesktop.org/secret-service/re01.html */
/* dbus object "path:/org/freedesktop/secrets, iface:xorg.freedesktop.Secret.Service" */
static struct secrets_object *g_service_object;

/* http://standards.freedesktop.org/secret-service/ch06.html */
/* proxy for dbus object "path:/org/freedesktop/secrets/session/ssss, iface:xorg.freedesktop.Secret.Session" */
/* where ssss is an auto-generated session specific identifier. */
static GDBusProxy *g_session_proxy;

/******************************************************************************/
/* helpers                                                                    */
/******************************************************************************/
static GDBusProxy *get_dbus_proxy(const gchar *path, const gchar *interface)
{
    GError *error = NULL;
    GDBusProxy *const proxy = g_dbus_proxy_new_sync(g_connection,
                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                    /* GDBusInterfaceInfo */ NULL,
                                                    SECRETS_SERVICE_BUS,
                                                    path,
                                                    interface,
                                                    /* GCancellable */ NULL,
                                                    &error);

    if (error)
    {
        error_msg(_("Can't connect over DBus to name '%s' path '%s' interface '%s': %s"),
                    SECRETS_SERVICE_BUS, path, interface, error->message);
        g_error_free(error);
        /* proxy is NULL in this case */
    }
    return proxy;
}

static GVariant *dbus_call_sync(GDBusProxy *proxy, const gchar *method, GVariant *args)
{
    GError *error = NULL;
    GVariant *const resp = g_dbus_proxy_call_sync(proxy,
                                                  method,
                                                  args,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  SECRETS_CALL_DEFAULT_TIMEOUT,
                                                  /* GCancellable */ NULL,
                                                  &error);
    if (error)
    {
        error_msg(_("Can't call method '%s' over DBus on path '%s' interface '%s': %s"),
                    method,
                    g_dbus_proxy_get_object_path(proxy),
                    g_dbus_proxy_get_interface_name(proxy),
                    error->message);
        g_error_free(error);
        /* resp is NULL in this case */
    }
    return resp;
}

/* Compares D-Bus error name against a name passsed as type argument
 *
 * @param error an error occured in any D-Bus method
 * @param type a checked error type
 * @returns true if a D-Bus name of error param equals to a value of type param
 */
static bool is_dbus_remote_error(GError *error, const char *type)
{
    /* returns a malloced string and return value can be NULL */
    char *remote_type = g_dbus_error_get_remote_error(error);
    const bool result = (remote_type && strcmp(type, remote_type) == 0);
    g_free(remote_type);
    return result;
}

/******************************************************************************/
/* struct secrets_object                                                      */
/******************************************************************************/

static struct secrets_object *secrets_object_new_from_proxy(GDBusProxy *proxy)
{
    struct secrets_object *obj = xzalloc(sizeof(*obj));
    obj->proxy = proxy;
    obj->interface_name = g_dbus_proxy_get_interface_name(proxy);

    return obj;
}

static struct secrets_object *secrets_object_new(const gchar *path,
                                                 const gchar *interface)
{
    GDBusProxy *const proxy = get_dbus_proxy(path, interface);

    if (!proxy)
        return NULL;

    return secrets_object_new_from_proxy(proxy);
}

static bool secrets_object_change_path(struct secrets_object *obj,
                                       const gchar *path)
{
    /* Do not set a new path if is the same as the current path */
    if (strcmp(path, g_dbus_proxy_get_object_path(obj->proxy)) == 0)
        return true;

    GDBusProxy *const proxy = get_dbus_proxy(path, obj->interface_name);

    if (!proxy)
        return false;

    /* release the old DBus property proxy */
    if (obj->property_proxy)
    {
        g_object_unref(obj->property_proxy);
        obj->property_proxy = NULL;
    }

    /* release the old proxy */
    if (obj->proxy)
        g_object_unref(obj->proxy);

    obj->proxy = proxy;
    obj->interface_name = g_dbus_proxy_get_interface_name(proxy);
    return true;
}

static void secrets_object_delete(struct secrets_object *obj)
{
    if (!obj)
        return;

    if (obj->proxy)
        g_object_unref(obj->proxy);

    if (obj->property_proxy)
        g_object_unref(obj->property_proxy);

    free(obj);
}

static GDBusProxy *secrets_object_get_properties_proxy(struct secrets_object *obj)
{
    if (!obj->property_proxy)
        obj->property_proxy = get_dbus_proxy(g_dbus_proxy_get_object_path(obj->proxy),
                                             "org.freedesktop.DBus.Properties");

    return obj->property_proxy;
}

/*******************************************************************************/
/* struct secrets_service                                                        */
/*******************************************************************************/

static void secrets_service_set_unavailable(void);

/*
 * Initialize all global variables
 */
static enum secrets_service_state secrets_service_connect(void)
{
    GError *error = NULL;
    g_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (!g_connection)
    {
        log_warning("Failed to open connection to D-Bus session bus: %s", error->message);
        g_error_free(error);
        return SBS_UNAVAILABLE;
    }

    g_service_object = secrets_object_new("/org/freedesktop/secrets", SECRETS_NAME_SPACE("Service"));

    if (!g_service_object)
        return SBS_UNAVAILABLE;

    /* OpenSession (IN String algorithm, IN Variant input, OUT Variant output, OUT ObjectPath result); */
    /*   Open a unique session for the caller application. */
    /*   algorithm : The algorithm the caller wishes to use. */
    /*   input     : Input arguments for the algorithm. */
    /*   output    : Output of the session algorithm negotiation. */
    /*   result    : The object path of the session, if session was created. */
    GVariant *const resp = dbus_call_sync(g_service_object->proxy,
                                          "OpenSession",
                                          g_variant_new("(sv)", "plain", g_variant_new_string("")));

    if (!resp)
        return SBS_UNAVAILABLE;

    GVariant *const var = g_variant_get_child_value(resp, 1);

    g_variant_unref(resp);

    const gchar *const session_path = g_variant_get_string(var, NULL);
    g_session_proxy = get_dbus_proxy(session_path, SECRETS_NAME_SPACE("Session"));

    g_variant_unref(var);

    if (!g_session_proxy)
        return SBS_UNAVAILABLE;

    return SBS_CONNECTED;
}

static void secrets_service_close_connection(void)
{
    if (g_state != SBS_CONNECTED)
        return;

    /* Close (void);  */
    /*   Close this session. */
    GVariant *const resp = dbus_call_sync(g_session_proxy, "Close", g_variant_new("()"));

    if (resp)
        g_variant_unref(resp);
    /*else : don't take care about errors */

    g_object_unref(g_session_proxy);
    g_session_proxy = NULL;

    secrets_object_delete(g_service_object);
    g_service_object = NULL;

    g_object_unref(g_connection);
    g_connection = NULL;

    g_state = SBS_INITIAL;
}

static void secrets_service_set_unavailable(void)
{
    secrets_service_close_connection();

    g_state = SBS_UNAVAILABLE;
}

/*******************************************************************************/
/* org.freedesktop.Secret.Prompt                                               */
/* http://standards.freedesktop.org/secret-service/re05.html                   */
/*******************************************************************************/

/*
 * Holds all data necessary for management of the glib main loop with the
 * timeout dialog.
 */
struct secrets_loop_env
{
    GMainContext* gcontext;
    GMainLoop *gloop;
    GSource *timeout_source;
    GtkWidget *timeout_dialog;
};

struct prompt_source
{
    GSource source;
    GDBusProxy *prompt_proxy;
    struct secrets_loop_env *env;
};

struct prompt_call_back_args
{
    gboolean dismissed;
    GVariant *result;
    struct secrets_loop_env *env;
};

static void nuke_timeout_source(GSource *source)
{
    if (NULL == source)
        return;

    if (!g_source_is_destroyed(source))
        /* detach it from GMainContext if it is attached */
        g_source_destroy(source);

    g_source_unref(source);
}

static void prompt_quit_loop(struct secrets_loop_env *env)
{
    if (NULL != env->timeout_dialog)
    {
        gtk_widget_destroy(env->timeout_dialog);
        env->timeout_dialog = NULL;
    }

    nuke_timeout_source(env->timeout_source);
    env->timeout_source = NULL;

    if (g_main_loop_is_running(env->gloop))
        g_main_loop_quit(env->gloop);
}

static gboolean prompt_g_idle_prepare(GSource *source_data, gint *timeout)
{
    return TRUE;
}

static gboolean prompt_g_idle_check(GSource *source)
{
    return TRUE;
}

/* one shot dispatch function which runs a prompt in an event loop */
static gboolean prompt_g_idle_dispatch(GSource *source,
                                       GSourceFunc callback,
                                       gpointer user_data)
{
    struct prompt_source *const prompt_source = (struct prompt_source *)source;
    GDBusProxy *const prompt_proxy = prompt_source->prompt_proxy;

    /* Prompt (IN String window-id); */
    /*   Perform the prompt. A window should appear. */
    /*   window-id : Platform specific window handle to use for showing the prompt. */
    GVariant *const resp = dbus_call_sync(prompt_proxy,
                                          "Prompt",
                                          g_variant_new("(s)", ""));

    if (!resp)
        /* We have to kill the loop because a dbus call failed and the signal */
        /* which stops the loop won't be received */
        prompt_quit_loop(prompt_source->env);
    else
        g_variant_unref(resp);

    /* always return FALSE, it means that this source is to be removed from idle loop */
    return FALSE;
}

/* Prompt Completed signal callback */
static void prompt_call_back(GDBusConnection *connection, const gchar *sender_name,
                             const gchar *object_path, const gchar *interface_name,
                             const gchar *signal_name, GVariant *parameters,
                             gpointer user_data)
{
    struct prompt_call_back_args *const args = (struct prompt_call_back_args *)user_data;
    GVariant *result = NULL;

    /* Completed (IN Boolean dismissed, IN Variant result); */
    /*   The prompt and operation completed. */
    /*   dismissed : Whether the prompt and operation were dismissed or not. */
    /*   result    : The possibly empty, operation specific, result. */
    g_variant_get(parameters, "(bv)", &args->dismissed, &result);

    if (!args->dismissed)
        args->result = result;
    else
        /* if the prompt or operation were dismissed we don't care about result */
        g_variant_unref(result);

    prompt_quit_loop(args->env);
}

/*
 * If a prompt path is '/' then it isn't required to perform prompt.
 * Even more, prompt can't be performed because the path is not valid.
 */
static bool is_prompt_required(const char *prompt_path)
{
    return prompt_path && !(prompt_path[0] == '/' && prompt_path[1] == '\0');
}

/*
 * Sets the timeout dialog to be displayed after PROMPT_TIMEOUT_SECONDS.
 *
 * @param env Crate holding necessary values.
 */
static void prompt_timeout_start(struct secrets_loop_env *env);

/*
 * A response callback which stops main loop execution or starts a new timeout.
 *
 * The main loop is stopped if user replied with "YES"; otherwise a new timeout
 * is started.
 */
static void timeout_dialog_response_cb(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    struct secrets_loop_env *env = (struct secrets_loop_env *)user_data;

    gtk_widget_hide(env->timeout_dialog);

    /* YES means 'Yes I want to stop waiting for Secret Service' */
    /* for other responses we have to start a new timeout */
    if (GTK_RESPONSE_YES == response_id)
        prompt_quit_loop(env);
    else
        prompt_timeout_start(env);
}

/*
 * Shows the timeout dialog which should allow user to break infinite waiting
 * for Completed signal emitted by the Secret Service
 *
 * @param user_data Should hold an pointer to struct secrets_loop_env
 */
static gboolean prompt_timeout_cb(gpointer user_data)
{
    log_debug("A timeout was reached while waiting for the DBus Secret Service to get prompt result.");
    struct secrets_loop_env *env = (struct secrets_loop_env *)user_data;

    if (env->timeout_dialog == NULL)
    {
        GtkWidget *dialog = gtk_message_dialog_new(/*parent widget*/NULL,
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_WARNING,
                                                   GTK_BUTTONS_YES_NO,
                _("A timeout was reached while waiting for a prompt result from the DBus Secret Service."));

        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                _("Do you want to stop waiting and continue in reporting without properly loaded configuration?"));

        g_signal_connect(dialog, "response", G_CALLBACK(timeout_dialog_response_cb), user_data);

        env->timeout_dialog = dialog;
    }

    gtk_widget_show_all(env->timeout_dialog);

    return FALSE; /* remove this source */
}

/*
 * Sets the timeout dialog to be displayed after PROMPT_TIMEOUT_SECONDS.
 *
 * @param env Crate holding necessary values.
 */
static void prompt_timeout_start(struct secrets_loop_env *env)
{
    /* Clean up the old timeout source, it can't be attached to the context again.
     * http://developer.gnome.org/glib/2.34/glib-The-Main-Event-Loop.html#g-source-destroy
     *
     * The old timeout source was destroyed by GMainContext because source's
     * callback (prompt_timeout_cb) returns FALSE. The callback must return
     * FALSE because neither the expiration time of timeout source can be reset
     * nor timeout can be suspended.
     */
    nuke_timeout_source(env->timeout_source);

    env->timeout_source= g_timeout_source_new_seconds(PROMPT_TIMEOUT_SECONDS);

    g_source_set_callback(env->timeout_source, prompt_timeout_cb, /*callback arg*/env, /*arg destroyer*/NULL);
    g_source_attach(env->timeout_source, env->gcontext);
}

/*
 * Perform a prompt
 *
 * @param prompt_path An object path pointing to a prompt
 * @param result A prompt result (can be NULL)
 * @param dismissed A dismissed flag (can't be NULL)
 * @return returns TRUE if no errors occurred; otherwise false
 */
static GVariant *secrets_prompt(const char *prompt_path,
                                bool *dismissed)
{
    static GSourceFuncs idle_funcs = {
        .prepare = prompt_g_idle_prepare,
        .check = prompt_g_idle_check,
        .dispatch = prompt_g_idle_dispatch,
        .finalize = NULL,
        .closure_callback = NULL,
        .closure_marshal = NULL,
    };

    /* TODO : leak */
    GDBusProxy *const prompt_proxy = get_dbus_proxy(prompt_path, SECRETS_NAME_SPACE("Prompt"));

    *dismissed = false;
    if (!prompt_proxy)
        return NULL;

    /* We have to use the thread default main context because a dbus signal callback */
    /* will be invoked in the thread default main loop */
    GMainContext *context = g_main_context_get_thread_default();
    struct secrets_loop_env env = {
        .gcontext=context,
        .gloop=g_main_loop_new(context, FALSE),
        .timeout_source=NULL,
        .timeout_dialog=NULL,
    };

    struct prompt_call_back_args args = {
        .result=NULL,
        .dismissed=FALSE,
        .env=&env
    };

    /* g_dbus_connection_signal_subscribe() doesn't report any error */
    const guint signal_ret =
        g_dbus_connection_signal_subscribe(g_connection, SECRETS_SERVICE_BUS, SECRETS_NAME_SPACE("Prompt"),
                                           "Completed", prompt_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                           prompt_call_back, &args, NULL);

    /* prepare the prompt source */
    /* The promp source is implemented as idle source because we have to perform */
    /* a prompt in some event loop. We need an event loop because a promp operation is */
    /* perfomed assynchronously and we have to wait on the Completed signal. */
    /* Idle source simply performs a prompt after the loop is stared. */
    struct prompt_source *const prompt_source =
        (struct prompt_source*)g_source_new(&idle_funcs, sizeof(*prompt_source));
    prompt_source->prompt_proxy = prompt_proxy;
    prompt_source->env = &env;

    g_source_attach((GSource*)prompt_source, context);

    /* the loop may sucks in infinite loop if the signal is never received */
    /* thus in order to prevent it we use this timeout */
    prompt_timeout_start(&env);

    /* the loop is exited when the Completed signal is received */
    g_main_loop_run(env.gloop);

    /* destroy prompt source */
    g_object_unref(prompt_proxy);
    g_source_destroy((GSource*)prompt_source);
    g_source_unref((GSource*)prompt_source);

    g_dbus_connection_signal_unsubscribe(g_connection, signal_ret);
    g_main_loop_unref(env.gloop);

    *dismissed = args.dismissed;
    return args.result;
}

/*******************************************************************************/
/* org.freedesktop.Secret.Service                                              */
/* http://standards.freedesktop.org/secret-service/re01.html                   */
/*******************************************************************************/

static GVariant *secrets_service_unlock(const gchar *const *objects,
                                        bool *dismissed)
{
    /* Unlock (IN Array<ObjectPath> objects, OUT Array<ObjectPath> unlocked, OUT ObjectPath prompt); */
    /*   Unlock the specified objects. */
    /*   objects  :  Objects to unlock. */
    /*   unlocked :  Objects that were unlocked without a prompt. */
    /*   prompt   :  A prompt object which can be used to unlock the remaining objects, or the special value '/' when no prompt is necessary. */
    GVariant *const var = dbus_call_sync(g_service_object->proxy,
                                         "Unlock",
                                         g_variant_new("(^ao)", objects));
    if (!var)
        return false;

    gchar *prompt = NULL;
    GVariant *result = NULL;
    /* TODO : leak */
    g_variant_get(var, "(@aoo)", &result, &prompt);

    *dismissed = false;

    if (is_prompt_required(prompt))
    {
        g_variant_unref(result);
        result = secrets_prompt(prompt, dismissed);
    }

    g_variant_unref(var);

    return result;
}

static bool secrets_service_unlock_object(struct secrets_object *obj,
                                          bool *dismissed)
{
    /* objects to unlock */
    const gchar *const locked[] = {
        g_dbus_proxy_get_object_path(obj->proxy),
        NULL
    };

    GVariant *result = secrets_service_unlock(locked, dismissed);

    if (result)
    {
        gsize length = 0;
        const gchar *const *unlocked = g_variant_get_objv(result, &length);

        /* ksecrets doesn't set dismissed correctly */
        if (length)
        {
            const bool succ = secrets_object_change_path(obj, *unlocked);
            g_free((gpointer)unlocked);
            g_variant_unref(result);

            return succ;
        }
        else
        {
            *dismissed = true;
            g_variant_unref(result);
        }
    }

    /* the function is successful if result is not NULL or */
    /* if user dismissed any prompt */
    return result || *dismissed;
}

/*
 * KSecretsService doesn't implement ReadAlias
 *
 * The function iterates over all collections and compares their Labels
 * with given alias.
 *
 * Collection is null on error or if it doesn't exist
 */
static bool secrets_service_find_collection(const char *alias, struct secrets_object **collection)
{
    /* do not unref this proxy, the proxy is owned by secrets object */
    GDBusProxy *const properties = secrets_object_get_properties_proxy(g_service_object);

    if (!properties)
        return false;

    /* cannot use g_dbus_proxy_get_cached_property() because of */
    /* errors in case when secrets service was restarted during session */
    /* http://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces-properties */
    /* org.freedesktop.DBus.Properties.Get (IN String interface_name, IN String property_name, OUT Variant value); */
    GVariant *const prop = dbus_call_sync(properties,
                                          "Get",
                                           g_variant_new("(ss)",
                                                         SECRETS_NAME_SPACE("Service"),
                                                         "Collections")
                                          );
    if (!prop)
        return false;

    *collection = NULL;

    /* Returned value */
    GVariant *const value = g_variant_get_child_value(prop, 0);
    /* Contentet of returned value */
    GVariant *const list = g_variant_get_child_value(value, 0);

    GVariantIter *iter = NULL;
    g_variant_get(g_variant_get_child_value(value, 0), "ao", &iter);

    bool found = false;
    gchar *path = NULL;
    while (g_variant_iter_loop(iter, "o", &path))
    {
        GDBusProxy *const collection_proxy = get_dbus_proxy(path,
                                                            SECRETS_NAME_SPACE("Collection"));
        /* an error occurred */
        if (!collection_proxy)
            break;

        GVariant *const lbl_var = g_dbus_proxy_get_cached_property(collection_proxy,
                                                                  "Label");

        const gchar *const label = g_variant_get_string(lbl_var, NULL);
        found = label && (strcmp(label, alias) == 0);

        g_variant_unref(lbl_var);

        if (found)
        {
            *collection = secrets_object_new_from_proxy(collection_proxy);
            /* the collection_proxy variable will be unrefed byt the collection object */
            break;
        }

        g_object_unref(collection_proxy);
    }

    g_variant_iter_free(iter);
    g_variant_unref(list);
    g_variant_unref(value);
    g_variant_unref(prop);

    /* If coolection is not found no error could occure */
    /* If coolection is found and variable is null then error occurred */
    return !found || *collection;
}

/*
 * Collection is null on error or if it doesn't exist
 */
static bool secrets_service_read_alias(const char *alias, struct secrets_object **collection)
{
    GError *error = NULL;
    /* use dbus_call_sync if KSecrets doesn't require special handling */
    /* ReadAlias (IN String name, OUT ObjectPath collection); */
    /*   Get the collection with the given alias. */
    /*   name       : An alias, such as 'default'. */
    /*   collection : The collection or the path '/' if no such collection exists. */
    GVariant *const resp =
        g_dbus_proxy_call_sync(g_service_object->proxy, "ReadAlias", g_variant_new("(s)", alias),
                               G_DBUS_PROXY_FLAGS_NONE, SECRETS_CALL_DEFAULT_TIMEOUT,
                               NULL, &error);

    if (!error)
    {
        GVariant *const obj_path = g_variant_get_child_value(resp, 0);
        const gchar *const coll_path = g_variant_get_string(obj_path, NULL);

        /* found if the path is not "/" */
        const bool found = coll_path[0] != '/' || coll_path[1] != '\0';

        if (found)
            *collection = secrets_object_new(coll_path, SECRETS_NAME_SPACE("Collection"));

        g_variant_unref(obj_path);
        g_variant_unref(resp);

        /* return TRUE if collection was not found or if collection object is not NULL */
        return !found || *collection;
    }
    else if (is_dbus_remote_error(error, NOT_IMPLEMENTED_READ_ALIAS_ERROR))
    {
        /* this code branch can be safely removed if KSecrets provides ReadAlias method*/
        log_notice("D-Bus Secrets Service ReadAlias method failed,"
                  "going to search on the client side : %s", error->message);
        g_error_free(error);

        /* ksecrets doesn't implement ReadAlias, therefore search by label is performed */
        return secrets_service_find_collection(alias, collection);
    }

    error_msg(_("D-Bus Secrets Service ReadAlias('%s') method failed: %s"), alias, error->message);
    g_error_free(error);
    return false;
}

/*
 * Creates a new secrets service collection.
 *
 * @param label a collection label
 * @param alias a collection alias, empty string means default (see Secrets Service API)
 * @param dismissed a dismissed flag, true value means that user dismissed creation
 *                  the param never holds true if error occurred
 * @return on error returns false, otherwise returns true
 */
static struct secrets_object *secrets_service_create_collection(const gchar *label,
                                                                const gchar *alias,
                                                                bool *dismissed)
{
    GVariantBuilder *const prop_builder = g_variant_builder_new(G_VARIANT_TYPE_DICTIONARY);

    g_variant_builder_add(prop_builder, "{sv}", "org.freedesktop.Secret.Collection.Label",
                          g_variant_new_string(label) );

    /* CreateCollection (IN Dict<String,Variant> properties, IN String alias, */
    /*                   OUT ObjectPath collection, OUT ObjectPath prompt); */
    /*   Create a new collection with the specified properties. */
    /*   properties : Properties for the new collection. This allows setting the */
    /*                new collection's properties upon its creation. All */
    /*                READWRITE properties are useable. Specify the property */
    /*                names in full interface.Property form. */
    /*   alias      : If creating this connection for a well known alias then */
    /*                a string like default. If an collection with this */
    /*                well-known alias already exists, then that collection */
    /*                will be returned instead of creating a new collection. */
    /*                Any readwrite properties provided to this function will */
    /*                be set on the collection. */
    /*                Set this to an empty string if the new collection should */
    /*                not be associated with a well known alias. */
    /*  collection  : The new collection object, or '/' if prompting is */
    /*                necessary. */
    /*  prompt      : A prompt object if prompting is necessary, or '/' if no */
    /*                prompt was needed. */
    GVariant *const resp = dbus_call_sync(g_service_object->proxy,
                                          "CreateCollection",
                                           g_variant_new("(@a{sv}s)",
                                                         g_variant_builder_end(prop_builder),
                                                         alias)
                                         );

    if (!resp)
        return NULL;

    struct secrets_object *collection = NULL;

    gchar *prompt = NULL;
    GVariant *collection_var = NULL;
    /* TODO : leak */
    g_variant_get(resp, "(@oo)", &collection_var, &prompt);

    if (is_prompt_required(prompt))
    {
        g_variant_unref(collection_var);
        collection_var = secrets_prompt(prompt, dismissed);
    }

    if (collection_var)
    {
        gsize length = 0;
        const gchar *const coll_path = g_variant_get_string(collection_var, &length);

        /* ksecrets doens't properly set dismissed */
        /* if dismissed is false and result is empty */
        /* then ksecrets didn't properly set dismissed */
        if (length)
            collection = secrets_object_new(coll_path, SECRETS_NAME_SPACE("Collection"));
        else
            *dismissed = true;

        g_variant_unref(collection_var);
    }

    g_variant_unref(resp);

    return collection;
}

/*******************************************************************************/
/* org.freedesktop.Secret.Collection                                           */
/* http://standards.freedesktop.org/secret-service/re02.html                   */
/*******************************************************************************/

static GVariant *create_item_call_args(const char *event_name,
                                       GVariant *attributes,
                                       GVariant *secret,
                                       bool full_property_name)
{
    /* gnome-keyring accepts properties with namespace */
    /* {
     *   "org.freedesktop.Secret.Item.Label": 'event name',
     *   "org.freedesktop.Secret.Item.Attributes": {
     *                                               "BugzillaURL": "Value1",
     *                                               "BugzillaPassword": "Value2"
     *                                             }
     * }
     */

    /* ksecrets accepts properties without namespace */
    /* {
     *   "Label": 'event name',
     *   "Attributes": {
     *                   "BugzillaURL": "Value1",
     *                   "BugzillaPassword": "Value2"
     *                 }
     * }
     */
    const char *const lbl_name = full_property_name ? SECRETS_NAME_SPACE("Item.Label")
                                                    : "Label";
    const char *const att_name = full_property_name ? SECRETS_NAME_SPACE("Item.Attributes")
                                                    : "Attributes";

    GVariantBuilder *const prop_builder = g_variant_builder_new(G_VARIANT_TYPE_DICTIONARY);

    g_variant_builder_add(prop_builder, "{sv}", lbl_name, g_variant_new_string(event_name));
    g_variant_builder_add(prop_builder, "{sv}", att_name, attributes);

    GVariant *const prop = g_variant_builder_end(prop_builder);

    return g_variant_new("(@a{sv}@(oayays)b)", prop, secret, FALSE);
}

static GVariant *create_secret_from_options(GDBusProxy *session, GList *options, bool store_passwords)
{
    GVariantBuilder *const value_builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
    for (GList *iter = g_list_first(options); iter; iter = g_list_next(iter))
    {
        event_option_t *const op = (event_option_t *)iter->data;
        /* TODO : is it still necessary? Passwords are encrypted now. */
        if (op->eo_value != NULL &&
                (op->eo_type != OPTION_TYPE_PASSWORD || store_passwords))
        {
            const char *byte = op->eo_name;
            while(byte[0] != '\0')
                g_variant_builder_add(value_builder, "y", *byte++);

            g_variant_builder_add(value_builder, "y", SECRETS_OPTION_VALUE_DELIMITER);

            byte = op->eo_value;
            while(byte[0] != '\0')
                g_variant_builder_add(value_builder, "y", *byte++);

            g_variant_builder_add(value_builder, "y", '\0');
        }
    }

    GVariant *const value = g_variant_builder_end(value_builder);

    return g_variant_new("(oay@ays)",
                         g_dbus_proxy_get_object_path(session),
                         /* param */ NULL,
                         value,
                         "application/libreport");
}

static GVariant *create_lookup_attributes(const char *event_name)
{
    GVariantBuilder *const attr_builder = g_variant_builder_new(G_VARIANT_TYPE_DICTIONARY);
    /* add extra attribute used for searching */
    g_variant_builder_add(attr_builder, "{ss}", SECRETS_SEARCH_ATTRIBUTE, event_name);

    /* An example of attributes:               */
    /* {                                       */
    /*   "libreportEventConfig": "event_name", */
    /* }                                       */
    return g_variant_builder_end(attr_builder);
}

static bool secrets_collection_create_text_item(struct secrets_object *collection,
                                                const char *event_name,
                                                GVariant *attributes,
                                                GVariant *secret,
                                                bool *dismissed)
{
    bool succ = false;
    GError *error = NULL;

    /* not dismissed by default */
    *dismissed = false;

    /* ksecrets silently drops properties with full names */
    /* gnome-keyring returns an error if a property key is not full property name */
    /* the first iteration sends property names without namespace */
    /* if NO error was returned service is ksecrets and everything is ok */
    /* if error was returned service is gnome-keyring and the second iteration */
    /*   must be performed */
    /* the second iteration sends property names with namespace */
    static bool const options[] = {FALSE, TRUE};
    for (size_t choice = 0; choice < sizeof(options)/sizeof(*options); ++choice)
    {
        if (error)
        {   /* this code is here because I want to know */
            /* if an error occurred from outside the loop */
            /* it cannot be on the end of the loop because */
            /* I need unfreed error after the last iteration */
            g_error_free(error);
            error = NULL;
        }

        /* CreateItem (IN Dict<String,Variant> properties, IN Secret secret, */
        /*             IN Boolean replace, OUT ObjectPath item, */
        /*             OUT ObjectPath prompt); */
        /*   Create an item with the given attributes, secret and label. If */
        /*   replace is set, then it replaces an item already present with the */
        /*   same values for the attributes. */
        /*   properties : The properties for the new item. This allows setting */
        /*                the new item's properties upon its creation. All */
        /*                READWRITE properties are useable. Specify the */
        /*                property names in full interface.Property form. */
        /*   secret     : The secret to store in the item, encoded with the */
        /*                included session. */
        /*   replace    : Whether to replace an item with the same attributes */
        /*                or not. */
        /*   item       : The item created, or the special value '/' if a prompt */
        /*                is necessary. */
        /*   prompt     : A prompt object, or the special value '/' if no prompt */
        /*                is necessary. */
        GVariant *const resp = g_dbus_proxy_call_sync(collection->proxy, "CreateItem",
                                      create_item_call_args(event_name,
                                                            g_variant_ref(attributes),
                                                            g_variant_ref(secret),
                                                            options[choice]),
                                      G_DBUS_PROXY_FLAGS_NONE, SECRETS_CALL_DEFAULT_TIMEOUT,
                                      NULL, &error);

        if (error)
        {
            if (is_dbus_remote_error(error, INVALID_PROPERTIES_ARGUMENTS_ERROR))
            {  /* it is OK - we know this error and we can safely continue */
               log_info("CreateItem failed, going to use other property names: %s", error->message);
               continue;
            }

            /* if the error wasn't about invalid properties we have an another problem */
            error_msg(_("Can't create a secret item for event '%s': %s"), event_name, error->message);
            g_error_free(error);
            break;
        }

        gchar *prompt = NULL;
        gchar *item = NULL;
        /* TODO : leak */
        g_variant_get(resp, "(oo)", &item, &prompt);

        /* if prompt is no required the function is successfull */
        /* therefore set return value to 'true' */
        succ = true;

        if (is_prompt_required(prompt))
            succ = secrets_prompt(prompt, dismissed) != NULL;

        g_variant_unref(resp);

        /* a dbus call was successfull, we don't need to try next type of property names */
        /* breaking the loop, nothing else to do */
        break;
    }

    g_variant_unref(attributes);
    g_variant_unref(secret);

    return succ;
}

/*
 * Performs org.freedesktop.Secret.Collection.SearchItems and returns the first
 * item from result. A found item can be possibly locked.
 */
static bool secrets_collection_search_one_item(const struct secrets_object *collection,
                                               GVariant *search_attrs,
                                               struct secrets_object **item)
{
    /* SearchItems (IN Dict<String,String> attributes, OUT Array<ObjectPath> results); */
    /*   Search for items in this collection matching the lookup attributes. */
    /*   attributes : Attributes to match. */
    /*   results    : Items that matched the attributes. */
    GVariant *const resp = dbus_call_sync(collection->proxy,
                                          "SearchItems",
                                          g_variant_new("(@a{ss})", search_attrs));

    if (!resp)
        return false;

    /* even if no item is found the function finishes successfully */
    bool found = false;
    *item = NULL;

    /* gnome-keyring returns (unlocked,locked) */
    /* ksecretsservice returns (unlocked) */
    /* all result childs has to be taken into account */
    const gsize n_results = g_variant_n_children(resp);

    for (gsize child = 0; !*item && child < n_results; ++child)
    {   /*               ^^^^^^ break if the item object was created */

        GVariant *const paths = g_variant_get_child_value(resp, child);

        /* NULL terminated list of path */
        const gchar *const *item_path_vector = g_variant_get_objv(paths, NULL);

        /* the first valid object path will be returned */
        if (*item_path_vector)
        {
            found = true;
            *item = secrets_object_new(*item_path_vector, SECRETS_NAME_SPACE("Item"));
            /* contniue on error - gnome-keyring unlocked result can */
            /*                     hold an invalid resp */
        }

        g_free((gpointer)item_path_vector);
        g_variant_unref(paths);
    }

    g_variant_unref(resp);

    /* If item is not found no error could occure */
    /* If item is found and variable is null then error occurred */
    return !found || *item;
}

/*******************************************************************************/
/* utility functions                                                           */
/*******************************************************************************/

/*
 * Gets an unlocked default collection if it exists
 *
 * Collection is null when error occurred or when user dismissed unlocking
 *
 * @return on error false
 */
static bool get_default_collection(struct secrets_object **collection,
                                   int flags,
                                   bool *dismissed)
{
    bool succ = secrets_service_read_alias(SECRETS_COLLECTION_ALIAS, collection);

    if (!succ)
        return false;

    /* ReadAlias was successful*/
    if (*collection != NULL && flags & GET_COLLECTION_UNLOCK)
    {   /* the default collection was found */
        succ = secrets_service_unlock_object(*collection, dismissed);

        if (!succ || *dismissed)
        {
            secrets_object_delete(*collection);
            *collection = NULL;
        }
    }

    if (*collection == NULL && flags & GET_COLLECTION_CREATE)
    {   /* the default collection wasn't found */
        /* a method caller requires to create a new collection */
        /* if the default collection doesn't exist */
        log_info("going to create a new default collection '"SECRETS_COLLECTION_LABEL"'"
                  " with alias '"SECRETS_COLLECTION_ALIAS"'");

        *collection = secrets_service_create_collection(SECRETS_COLLECTION_LABEL,
                                                        SECRETS_COLLECTION_ALIAS,
                                                        dismissed);

        /* create collection function succeded if a collection is not NULL */
        /* or if the call was dismissed */
        /* if dismissed is false and collection is null then an error occurred */
        succ = *collection || *dismissed;
    }

    return succ;
}

static void load_event_options_from_item(GDBusProxy *session,
                                         GList *options,
                                         struct secrets_object *item)
{
    {   /* for backward compatibility */
        /* load configuration from lookup attributes but don't store configuration in lookup attributes */
        GVariant *const attributes =
            g_dbus_proxy_get_cached_property(item->proxy, "Attributes");

        GVariantIter *iter = NULL;
        g_variant_get(attributes, "a{ss}", &iter);

        gchar *name = NULL;
        gchar *value = NULL;
        while (g_variant_iter_loop(iter, "{ss}", &name, &value))
        {
            event_option_t *const option = get_event_option_from_list(name, options);
            if (option)
            {
                free(option->eo_value);
                option->eo_value = xstrdup(value);
                log_info("loaded event option : '%s' => '%s'", name, option->eo_value);
            }
        }

        g_variant_unref(attributes);
    }

    {   /* read configuration from the secret value */
        GError *error = NULL;
        GVariant *const resp = g_dbus_proxy_call_sync(item->proxy,
                                                      "GetSecret",
                                                      g_variant_new("(o)",
                                                        g_dbus_proxy_get_object_path(session)),
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      SECRETS_CALL_DEFAULT_TIMEOUT,
                                                      /* GCancellable */ NULL,
                                                      &error);

        if (error)
        {   /* if the error is NOT the known error produced by the gnome-keyring */
            /* when no secret value is assigned to an item */
            /* then let you user known that the error occurred */
            if (is_dbus_remote_error(error, GNOME_KEYRING_NOT_HAVING_SECRET_ERROR))
                error_msg(_("can't get secret value of '%s': %s"), (const char *)item->tag, error->message);
            else
            {
                log_notice("can't get secret value: %s", error->message);
            }

            g_error_free(error);
            return;
        }

        if (g_variant_n_children(resp) == 0)
        {
            log_notice("response from GetSecret() method doesn't contain data: '%s'", g_variant_print(resp, TRUE));
            return;
        }

        /* All dbus responses are returned inside of an variant */
        GVariant *const secret = g_variant_get_child_value(resp, 0);

        /* DBus represents a structure as a tuple of values */
        /* struct Secret
         * {
         *   (o)  object path
         *   (ay) byte array params
         *   (ay) byte array value
         *   (s)  string content type
         * }
         */
        if (g_variant_n_children(secret) != 4)
        {
            log_notice("secret value has invalid structure: '%s'", g_variant_print(secret, TRUE));
            return;
        }

        /* get (ay) byte array value */
        GVariant *const secret_value = g_variant_get_child_value(secret, 2);

        gsize nelems;
        gconstpointer data = g_variant_get_fixed_array(secret_value, &nelems, sizeof(guchar));

        while(nelems > 0)
        {
            const size_t sz = strlen(data) + 1;
            if (sz > nelems)
            {   /* check next size, if is too big then trailing 0 is probably missing */
                log_notice("broken secret value, atttempts to read %zdB but only %zdB are available)", sz, nelems);
                break;
            }
            nelems -= sz;

            char *name = xstrdup(data);
            char *value = strchr(name, SECRETS_OPTION_VALUE_DELIMITER);
            if (!value)
            {
                log_notice("secret value has invalid format: missing delimiter after option name");
                goto next_option;
            }

            *value++ = '\0';

            event_option_t *const option = get_event_option_from_list(name, options);
            if (option)
            {
                free(option->eo_value);
                option->eo_value = xstrdup(value);
                log_info("loaded event option : '%s' => '%s'", name, option->eo_value);
            }

next_option:
            free(name);
            data += sz;
        }

        g_variant_unref(secret_value);
        g_variant_unref(secret);
        g_variant_unref(resp);
    }
}

static bool find_item_by_event_name(const struct secrets_object *collection,
                                    const char *event_name,
                                    struct secrets_object **item)
{
    return secrets_collection_search_one_item(collection, create_lookup_attributes(event_name), item);
}

static void load_settings(GDBusProxy *session, struct secrets_object *collection,
                          const char *event_name, event_config_t *ec)
{
    struct secrets_object *item = NULL;
    bool dismissed = false;
    log_info("looking for event config : '%s'", event_name);
    bool succ = find_item_by_event_name(collection, event_name, &item);
    if (succ && item)
    {
        succ = secrets_service_unlock_object(item, &dismissed);
        if (succ && !dismissed)
        {
            log_notice("loading event config : '%s'", event_name);
            item->tag = (void *)event_name;
            load_event_options_from_item(session, ec->options, item);
        }
        secrets_object_delete(item);
        item = NULL;
    }

    if (!succ || dismissed)
        secrets_service_set_unavailable();
}

static void load_event_config(gpointer key, gpointer value, gpointer user_data)
{
    char *const event_name = (char*)key;
    event_config_t *const ec = (event_config_t *)value;
    struct secrets_object *const collection = (struct secrets_object *)user_data;

    if (libreport_is_event_config_user_storage_available())
        load_settings(g_session_proxy, collection, event_name, ec);
}

static bool save_options(struct secrets_object *collection,
                         const char *event_name,
                         GList *options,
                         bool store_passwords,
                         bool *dismissed)
{
    struct secrets_object *item = NULL;
    bool succ = find_item_by_event_name(collection, event_name, &item);

    if (!succ)
        return succ;

    GVariant *const secret = create_secret_from_options(g_session_proxy, options, store_passwords);

    if (item)
    {   /* item exists, change a secret */
        log_info("updating event config : '%s'", event_name);
        /* can't be dismissed */
        *dismissed = false;
        GVariant *const args = g_variant_new("(@(oayays))", secret);
        GVariant *const resp = dbus_call_sync(item->proxy, "SetSecret", args);
        succ = resp;

        if (succ)
            g_variant_unref(resp);

        secrets_object_delete(item);
    }
    else
    {   /* create a new item */
        GVariant *const attributes = create_lookup_attributes(event_name);
        log_info("creating event config : '%s'", event_name);
        succ = secrets_collection_create_text_item(collection, event_name, attributes, secret, dismissed);
    }

    return succ;
}

static void save_event_config(const char *event_name,
                              GList *options,
                              bool store_passwords)
{
    bool dismissed = false;

    /* We have to handle four cases:
     *   1. The default collection exists and is unlocked
     *      collection is set to some value different than NULL
     *      succ is TRUE - call was successful
     *      dismissed is FALSE - a prompt was confirmed
     *      In this case everything is ok and we are happy
     *
     *   2. The default collection doesn't exist.
     *      collection is NULL
     *      succ is TRUE - call was successful
     *      dismissed is FALSE - no collection no prompt no dismissed
     *      In this case everything is ok and we are happy
     *
     *   3. User dismissed a prompt for unlocking of the default collection
     *      collection is NULL - collection is not unlocked thus we can't read it
     *      succ is TRUE - call was successful
     *      dismissed is TRUE - USER DISMISSED A PROMPT, he don't want to unlock the default collection !!
     *      We the set service state to unavailable in order to not bother user with prompts anymore
     *
     *   4. An error occurred
     *      collection is NULL
     *      succ is TRUE - call was NOT successful
     *      dismissed variable holds FALSE - no prompt no dismissed
     *      Set the service state to unavailable in order to not bother user with error messages anymore
     */
    struct secrets_object *collection = NULL;
    const int flags = GET_COLLECTION_UNLOCK | GET_COLLECTION_CREATE;
    bool succ = get_default_collection(&collection, flags, &dismissed);

    if (collection)
    {
        succ = save_options(collection, event_name, options, store_passwords, &dismissed);
        secrets_object_delete(collection);
        collection = NULL;
    }

    if (!succ || dismissed)
        secrets_service_set_unavailable();
}

/******************************************************************************/
/* Public interface                                                           */
/******************************************************************************/

bool libreport_is_event_config_user_storage_available()
{
    INITIALIZE_LIBREPORT();

    if (g_state == SBS_INITIAL)
        g_state = secrets_service_connect();

    return g_state != SBS_UNAVAILABLE;
}

/*
 * Loads event config options for passed event
 *
 * @param name Event name
 * @param config Event config
 */
void libreport_load_single_event_config_data_from_user_storage(event_config_t *config)
{
    INITIALIZE_LIBREPORT();

    GHashTable *tmp = g_hash_table_new_full(
                /*hash_func*/ g_str_hash,
                /*key_equal_func:*/ g_str_equal,
                /*key_destroy_func:*/ g_free,
                /*value_destroy_func:*/ NULL);

    g_hash_table_insert(tmp, xstrdup(ec_get_name(config)), config);

    libreport_load_event_config_data_from_user_storage(tmp);

    g_hash_table_destroy(tmp);

    return;
}

/*
 * Loads event config options for passed events
 *
 * @param event_config_list Events configs
 */
void libreport_load_event_config_data_from_user_storage(GHashTable *event_config_list)
{
    INITIALIZE_LIBREPORT();

    if (libreport_is_event_config_user_storage_available())
    {
        bool dismissed = false;
        struct secrets_object *collection = NULL;

        /* We have to handle four cases:
         *   1. The default collection exists and is unlocked
         *      collection is set to some value different than NULL
         *      succ is TRUE - call was successful
         *      dismissed is FALSE - a prompt was confirmed
         *      In this case everything is ok and we are happy
         *
         *   2. The default collection doesn't exist.
         *      collection is NULL
         *      succ is TRUE - call was successful
         *      dismissed is FALSE - no collection no prompt no dismissed
         *      In this case everything is ok and we are happy
         *
         *   3. User dismissed a prompt for unlocking of the default collection
         *      collection is NULL - collection is not unlocked thus we can't read it
         *      succ is TRUE - call was successful
         *      dismissed is TRUE - USER DISMISSED A PROMPT, he don't want to unlock the default collection !!
         *      We the set service state to unavailable in order to not bother user with prompts anymore
         *
         *   4. An error occurred
         *      collection is NULL
         *      succ is TRUE - call was NOT successful
         *      dismissed variable holds FALSE - no prompt no dismissed
         *      Set the service state to unavailable in order to not bother user with error messages anymore
         */
        const int flags = GET_COLLECTION_FLAGS_NONE;
        const bool succ = get_default_collection(&collection, flags, &dismissed);

        if (collection)
        {
            g_hash_table_foreach(event_config_list, &load_event_config, collection);
            secrets_object_delete(collection);
            collection = NULL;
        }

        if (!succ || dismissed)
            secrets_service_set_unavailable();
    }
    else
    {
        log_notice("Can't load user's configuration due to unavailability of D-Bus Secrets Service");
    }
}

/*
 * Saves an event_config options to some kind of session storage.
 *
 * @param event_name Lookup key
 * @param event_config Event data
 * @param store_passwords If TRUE stores options with passwords, otherwise skips them
 */
void libreport_save_event_config_data_to_user_storage(const char *event_name,
                                            const event_config_t *event_config,
                                            bool store_passwords)
{
    INITIALIZE_LIBREPORT();

    if (libreport_is_event_config_user_storage_available())
        save_event_config(event_name, event_config->options, store_passwords);
    else
    {
        log_notice("Can't save user's configuration due to unavailability of D-Bus secrets API");
    }
}
