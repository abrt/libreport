/* Copyright (C) 2019  Red Hat, Inc.
 *
 * libreport is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libreport is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libreport.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <client.h>
#include <types.h>
#include <ureport.h>
#include <utils.h>

#include <glib-object.h>

/* Using the same template as for RHSM certificate - macro for cert dir path and
 * macro for cert name. Cert path can be easily modified for example by reading
 * an environment variable LIBREPORT_DEBUG_AUTHORITY_CERT_DIR_PATH
 */
#define CERT_AUTHORITY_CERT_PATH "/etc/libreport"
#define CERT_AUTHORITY_CERT_NAME "cert-api.access.redhat.com.pem"
#define RHSMCON_CERT_NAME "cert.pem"
#define RHSMCON_KEY_NAME "key.pem"
#define RHSMCON_PEM_DIR_PATH "/etc/pki/consumer"
#define RHSM_WEB_SERVICE_URL "https://cert-api.access.redhat.com/rs/telemetry/abrt"

struct _UReportServerConfig
{
    char *url;         ///< Web service URL
    bool ssl_verify;   ///< Verify HOST and PEER certificates
    char *client_cert; ///< Path to certificate used for client
                       ///< authentication (or NULL)
    char *client_key;  ///< Private key for the certificate
    char *cert_authority_cert; ///< Certificate authority certificate
    char *username;    ///< username for basic HTTP auth
    char *password;    ///< password for basic HTTP auth

    GList *auth_items;              ///< list of file names included in 'auth' key
    UReportPreferencesFlags flags;
};

G_DEFINE_BOXED_TYPE(UReportServerConfig, ureport_server_config,
                    ureport_server_config_dup, ureport_server_config_destroy)

/**
 * ureport_server_config_get_auth_items:
 *
 * Returns: (element-type utf8) (transfer container)
 */
GList *
ureport_server_config_get_auth_items(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_list_copy(self->auth_items);
}

/**
 * ureport_server_config_set_auth_items:
 * @auth_items: (element-type utf8) (transfer full) (nullable)
 */
void
ureport_server_config_set_auth_items(UReportServerConfig *self,
                                     GList               *auth_items)
{
    g_return_if_fail(NULL != self);

    if (NULL != self->auth_items)
    {
        g_list_free_full(self->auth_items, g_free);
    }

    self->auth_items = auth_items;
}

void
ureport_server_config_set_basic_auth(UReportServerConfig *self,
                                     const char          *username,
                                     const char          *password)
{
    g_return_if_fail(NULL != self);

    ureport_server_config_set_client_auth(self, NULL);

    ureport_server_config_set_username(self, username);
    ureport_server_config_set_password(self, password);
}

static char *
puppet_config_print(const char *key)
{
    g_autofree char *command = NULL;
    g_autofree char *result = NULL;
    char *newline;

    command = xasprintf("puppet config print %s", key);
    result = run_in_shell_and_save_output(0, command, NULL, NULL);

    /* run_in_shell_and_save_output always returns non-NULL */
    if (!g_str_has_prefix(result, "/"))
    {
        error_msg_and_die("Unable to determine puppet %s path (puppet not installed?)", key);
    }

    newline = strchrnul(result, '\n');
    if (NULL == newline)
    {
        error_msg_and_die("Unable to determine puppet %s path (puppet not installed?)", key);
    }

    *newline = '\0';

    return g_steal_pointer(&result);
}

char *
ureport_server_config_get_client_cert(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->client_cert);
}

char *
ureport_server_config_get_client_key(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->client_key);
}

char *
ureport_server_config_get_cert_authority_cert(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->cert_authority_cert);
}

UReportPreferencesFlags
ureport_server_config_get_flags(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, 0);

    return self->flags;
}

void
ureport_server_config_set_flags(UReportServerConfig     *self,
                                UReportPreferencesFlags  flags)
{
    g_return_if_fail(NULL != self);

    self->flags = flags;
}

static char *
rhsm_config_get_consumer_cert_dir(void)
{
    char *env_path;
    const char *command_line;
    g_autofree char *result = NULL;

    env_path = getenv("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH");
    if (NULL != env_path)
    {
        return g_strdup(env_path);
    }
    command_line = "python3 -c"
                   "\""
                   "from rhsm.config import initConfig;"
                   "print(initConfig().get('rhsm', 'consumerCertDir'))"
                   "\"";
    result = run_in_shell_and_save_output(0, command_line, NULL, NULL);

    /* run_in_shell_and_save_output always returns non-NULL */
    if (g_str_has_prefix(result, "/"))
    {
        char *newline;

        newline = strchrnul(result, '\n');
        if (NULL != newline)
        {
            *newline = '\0';

            return g_steal_pointer(&result);
        }
    }

    error_msg("Failed to get 'rhsm':'consumerCertDir' from rhsm.config python module. Using "RHSMCON_PEM_DIR_PATH);

    return g_strdup(RHSMCON_PEM_DIR_PATH);
}

void
ureport_server_config_set_client_auth(UReportServerConfig *config,
                                      const char *client_auth)
{
    g_return_if_fail(NULL != config);

    if (NULL == client_auth || '\0' == *client_auth)
    {
        g_clear_pointer(&config->client_cert, g_free);
        g_clear_pointer(&config->client_key, g_free);

        g_debug("Not using client authentication");
    }
    else if (g_strcmp0(client_auth, "rhsm") == 0)
    {
        g_autofree char *rhsm_dir = NULL;
        g_autofree char *cert_full_name = NULL;
        g_autofree char *key_full_name = NULL;
        const char *authority_cert_dir_path;
        g_autofree char *cert_authority_cert_full_name = NULL;

        if (NULL == config->url)
        {
            ureport_server_config_set_url(config, RHSM_WEB_SERVICE_URL);
        }

        rhsm_dir = rhsm_config_get_consumer_cert_dir();
        cert_full_name = g_build_path(G_DIR_SEPARATOR_S, rhsm_dir, RHSMCON_CERT_NAME, NULL);
        key_full_name = g_build_path(G_DIR_SEPARATOR_S, rhsm_dir, RHSMCON_KEY_NAME, NULL);
        authority_cert_dir_path = getenv("LIBREPORT_DEBUG_AUTHORITY_CERT_DIR_PATH");
        if (authority_cert_dir_path == NULL)
        {
           authority_cert_dir_path = CERT_AUTHORITY_CERT_PATH;
        }
        cert_authority_cert_full_name = g_build_path(G_DIR_SEPARATOR_S,
                                                     authority_cert_dir_path,
                                                     CERT_AUTHORITY_CERT_NAME,
                                                     NULL);

        if (access(cert_full_name, F_OK) == 0 && access(key_full_name, F_OK) == 0)
        {
            config->client_cert = g_steal_pointer(&cert_full_name);
            config->client_key = g_steal_pointer(&key_full_name);

            g_debug("Using cert files: '%s' : '%s'", config->client_cert, config->client_key);
        }
        else
        {
            g_debug("RHSM consumer certificate “%s” or key “%s” does not exist",
                    cert_full_name, key_full_name);
            g_debug("Using the default configuration for uReports");
        }

        if (access(cert_authority_cert_full_name, F_OK) == 0)
        {
            config->cert_authority_cert = g_steal_pointer(&cert_authority_cert_full_name);

            g_debug("Using validating server cert: “%s”", config->cert_authority_cert);
        }
        else
        {
            g_debug("Certs validating the server “%s” does not exist.",
                    cert_authority_cert_full_name);
        }
    }
    else if (g_strcmp0(client_auth, "puppet") == 0)
    {
        config->client_cert = puppet_config_print("hostcert");
        config->client_key = puppet_config_print("hostprivkey");
    }
    else
    {
        g_auto(GStrv) credentials = NULL;

        credentials = g_strsplit(client_auth, ":", 2);

        if (g_strv_length (credentials) < 2 ||
            g_strcmp0 (credentials[0], "") == 0 ||
            g_strcmp0 (credentials[1], "") == 0)
        {
            error_msg_and_die("Invalid client authentication specification");
        }

        config->client_cert = g_steal_pointer(&credentials[0]);
        config->client_key = g_steal_pointer(&credentials[1]);
    }

    if (NULL != config->client_cert && NULL != config->client_key)
    {
        g_debug("Using client certificate: %s", config->client_cert);
        g_debug("Using client private key: %s", config->client_key);

        g_clear_pointer(&config->username, g_free);
        g_clear_pointer(&config->password, g_free);
    }
}

char *
ureport_server_config_get_password(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->password);
}

void
ureport_server_config_set_password(UReportServerConfig *self,
                                   const char          *password)
{
    g_return_if_fail(NULL != self);

    g_free(self->password);
    self->password = g_strdup(password);
}

bool
ureport_server_config_get_ssl_verify(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, false);

    return self->ssl_verify;
}

void
ureport_server_config_set_ssl_verify(UReportServerConfig *self,
                                     bool                 ssl_verify)
{
    g_return_if_fail(NULL != self);

    self->ssl_verify = ssl_verify;
}

char *
ureport_server_config_get_url(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->url);
}

void
ureport_server_config_set_url(UReportServerConfig *self,
                              const char          *url)
{
    g_return_if_fail(NULL != self);

    g_clear_pointer(&self->url, g_free);

    self->url = g_strdup(url);
}

char *
ureport_server_config_get_username(UReportServerConfig *self)
{
    g_return_val_if_fail(NULL != self, NULL);

    return g_strdup(self->username);
}

void
ureport_server_config_set_username(UReportServerConfig *self,
                                   const char          *username)
{
    g_return_if_fail(NULL != self);

    g_free(self->username);
    self->username = g_strdup(username);
}

UReportServerConfig *
ureport_server_config_new(void)
{
    UReportServerConfig *config;

    config = g_new0(UReportServerConfig, 1);

    config->ssl_verify = true;

    return config;
}

static inline const char *
ureport_server_config_get_setting(GHashTable *settings,
                                  const char *key)
{
    const char *value;

    value = g_hash_table_lookup(settings, key);
    if (NULL == value)
    {
        g_autofree char *env_key = NULL;

        env_key = g_strdup_printf("uReport_%s", key);

        return getenv(env_key);
    }

    return value;
}

UReportServerConfig *
ureport_server_config_new_for_settings(GHashTable *settings)
{
    UReportServerConfig *config;
    const char *setting;
    bool include_auth_data;

    g_return_val_if_fail(NULL != settings, NULL);

    config = ureport_server_config_new();

    setting = ureport_server_config_get_setting(settings, "URL");
    ureport_server_config_set_url(config, setting);
    setting = ureport_server_config_get_setting(settings, "SSLVerify");
    if (NULL != setting)
    {
        bool ssl_verify;

        ssl_verify = string_to_bool(setting);

        ureport_server_config_set_ssl_verify(config, ssl_verify);
    }
    setting = ureport_server_config_get_setting(settings, "HTTPAuth");
    if (NULL == setting)
    {
        setting = ureport_server_config_get_setting(settings, "SSLClientAuth");

        ureport_server_config_set_client_auth(config, setting);
    }
    else
    {
        ureport_server_config_load_basic_auth(config, setting);
    }
    setting = ureport_server_config_get_setting(settings, "IncludeAuthData");
    if (NULL == setting)
    {
        /* 9238648846e56f53f271bb1936a1a59191b350d5 for “explanation”. */
        include_auth_data = NULL != config->client_cert || NULL != config->username;
    }
    else
    {
        include_auth_data = string_to_bool(setting);
    }

    if (include_auth_data)
    {
        setting = ureport_server_config_get_setting(settings, "AuthDataItems");
        if (setting == NULL)
        {
            g_debug("IncludeAuthData enabled, but AuthDataItems is empty");
        }
        else
        {
            char **substrings;
            GList *auth_data_items = NULL;

            substrings = g_strsplit(setting, ",", -1);
            while (NULL != *substrings)
            {
                char *substring;

                substring = g_strstrip(*substrings);
                auth_data_items = g_list_prepend(auth_data_items, substring);

                substrings++;
            }
            auth_data_items = g_list_reverse(auth_data_items);

            ureport_server_config_set_auth_items(config, auth_data_items);
        }
    }

    return config;
}

UReportServerConfig *
ureport_server_config_dup(UReportServerConfig *self)
{
    UReportServerConfig *config;

    config = ureport_server_config_new();

    config->url = g_strdup(self->url);
    config->ssl_verify = self->ssl_verify;
    config->client_cert = g_strdup(self->client_cert);
    config->client_key = g_strdup(self->client_key);
    config->cert_authority_cert = g_strdup(self->cert_authority_cert);
    config->username = g_strdup(self->username);
    config->password = g_strdup(self->password);
    config->flags = self->flags;
    config->auth_items = g_list_copy_deep(self->auth_items,
                                          report_string_list_copy_func,
                                          NULL);

    return config;
}

void
ureport_server_config_destroy(UReportServerConfig *config)
{
    g_clear_pointer(&config->url, g_free);
    g_clear_pointer(&config->client_cert, g_free);
    g_clear_pointer(&config->client_key, g_free);
    g_clear_pointer(&config->cert_authority_cert, g_free);
    g_clear_pointer(&config->username, g_free);
    g_clear_pointer(&config->password, g_free);

    g_list_free_full(config->auth_items, g_free);
    config->auth_items = NULL;
}

void
ureport_server_config_load_basic_auth(UReportServerConfig *config,
                                      const char *http_auth_pref)
{
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;

    g_return_if_fail(NULL != http_auth_pref);
    g_return_if_fail('\0' != *http_auth_pref);

    if (strcmp(http_auth_pref, "rhts-credentials") == 0)
    {
        g_autoptr(GHashTable) settings = NULL;
        g_autofree char *local_conf = NULL;

        settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        local_conf = g_build_path(G_DIR_SEPARATOR_S,
                                  getenv("HOME"),
                                  USER_HOME_CONFIG_PATH,
                                  "rhtsupport.conf",
                                  NULL);

        if (!load_plugin_conf_file("rhtsupport.conf", settings, /*skip key w/o values:*/ false) &&
            !load_conf_file(local_conf, settings, /*skip key w/o values:*/ false))
        {
            error_msg_and_die("Could not get RHTSupport credentials");
        }

        username = g_hash_table_lookup(settings, "Login");
        password = g_hash_table_lookup(settings, "Password");

        (void)g_hash_table_steal(settings, "Login");
        (void)g_hash_table_steal(settings, "Password");

        if (NULL == config->url)
        {
            ureport_server_config_set_url(config, RHSM_WEB_SERVICE_URL);
        }
    }
    else
    {
        g_autofree GStrv credentials = NULL;

        credentials = g_strsplit(http_auth_pref, ":", 2);

        username = credentials[0];
        password = credentials[1];
    }

    if (NULL == password)
    {
        g_autofree char *message = NULL;

        message = g_strdup_printf("Please provide uReport server password for user '%s':", username);

        password = ask_password(message);
        if ('\0' == *password)
        {
            error_msg_and_die("Cannot continue without uReport server password!");
        }
    }

    ureport_server_config_set_basic_auth(config, username, password);
}
