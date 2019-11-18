#include <glib.h>
#include <libreport_curl.h>
#include <ureport.h>

#define BTHASH_URL_SUFFIX "reports/bthash/"
#define TESTING_CERTS_CORRECT_DIR_PATH "data/ureport/certs/correct"
#define TESTING_PYTHONPATH "data/ureport"
#define RHSM_WEB_SERVICE_URL "https://cert-api.access.redhat.com/rs/telemetry/abrt"
#define RHSMCON_PEM_DIR_PATH "/etc/pki/consumer"
#define WRONG_TESTING_PYTHONPATH "data/ureportxxxxxx/"

void
assert_ureport_server_config (UReportServerConfig *config,
                              const char          *url,
                              bool                 ssl_verify,
                              const char          *client_cert,
                              const char          *client_key,
                              const char          *username,
                              const char          *password)
{
    g_autofree char *config_url = NULL;
    bool config_ssl_verify;
    g_autofree char *config_client_cert = NULL;
    g_autofree char *config_client_key = NULL;
    g_autofree char *config_username = NULL;
    g_autofree char *config_password = NULL;

    config_url = ureport_server_config_get_url(config);
    config_ssl_verify = ureport_server_config_get_ssl_verify(config);
    config_client_cert = ureport_server_config_get_client_cert(config);
    config_client_key = ureport_server_config_get_client_key(config);
    config_username = ureport_server_config_get_username(config);
    config_password = ureport_server_config_get_password(config);

    g_assert_cmpstr(config_url, ==, url);
    assert(config_ssl_verify == ssl_verify);
    g_assert_cmpstr(config_client_cert, ==, client_cert);
    g_assert_cmpstr(config_client_key, ==, client_key);
    g_assert_cmpstr(config_username, ==, username);
    g_assert_cmpstr(config_password, ==, password);
}

static void
test_defaults (void)
{
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *ca_cert = NULL;
    g_autoptr (GList) auth_items = NULL;
    UReportPreferencesFlags flags;

    config = ureport_server_config_new ();
    ca_cert = ureport_server_config_get_cert_authority_cert (config);
    auth_items = ureport_server_config_get_auth_items (config);
    flags = ureport_server_config_get_flags (config);

    assert_ureport_server_config (config, NULL, true, NULL, NULL, NULL, NULL);

    g_assert_null (ca_cert);
    g_assert_null (auth_items);
    g_assert_cmpint (flags, ==, 0);
}

void
test_new_for_settings_1 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *url = NULL;
    bool ssl_verify;
    g_autoptr (GList) auth_items = NULL;

    settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert(settings, g_strdup("URL"), g_strdup("settings_url"));
    g_hash_table_insert(settings, g_strdup("SSLVerify"), g_strdup("yes"));
    g_hash_table_insert(settings, g_strdup("SSLClientAuth"), g_strdup(""));
    g_hash_table_insert(settings, g_strdup("IncludeAuthData"), g_strdup("no"));
    g_hash_table_insert(settings, g_strdup("AuthDataItems"), g_strdup("hostname"));

    config = ureport_server_config_new_for_settings (settings);
    url = ureport_server_config_get_url (config);
    ssl_verify = ureport_server_config_get_ssl_verify (config);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_cmpstr (url, ==, "settings_url");
    g_assert_true (ssl_verify);
    g_assert_null (auth_items);
}

/* IncludeAuthData set, but AuthDataItems not. */
void
test_new_for_settings_2 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autoptr (GList) auth_items = NULL;

    settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert (settings, g_strdup ("URL"), g_strdup ("settings_url"));
    g_hash_table_insert (settings, g_strdup ("SSLVerify"), g_strdup ("yes"));
    g_hash_table_insert (settings, g_strdup ("SSLClientAuth"), g_strdup (""));
    g_hash_table_insert (settings, g_strdup ("IncludeAuthData"), g_strdup ("yes"));
    g_hash_table_insert (settings, g_strdup ("AuthDataItems"), g_strdup (""));

    config = ureport_server_config_new_for_settings (settings);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_null (auth_items);
}

void
test_new_for_settings_3 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *url = NULL;
    bool ssl_verify;
    g_autoptr (GList) auth_items = NULL;

    settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert (settings, g_strdup("URL"), g_strdup("settings_url"));
    g_hash_table_insert (settings, g_strdup("SSLVerify"), g_strdup("no"));
    g_hash_table_insert (settings, g_strdup("SSLClientAuth"), g_strdup(""));
    g_hash_table_insert (settings, g_strdup("IncludeAuthData"), g_strdup("yes"));
    g_hash_table_insert (settings, g_strdup("AuthDataItems"), g_strdup("hostname, type"));

    config = ureport_server_config_new_for_settings (settings);
    url = ureport_server_config_get_url (config);
    ssl_verify = ureport_server_config_get_ssl_verify (config);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_cmpstr (url, ==, "settings_url");
    g_assert_false (ssl_verify);
    g_assert_cmpstr (auth_items->data, ==, "hostname");
    g_assert_cmpstr (auth_items->next->data, ==, "type");
}

void
test_new_for_settings_basic_auth (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;
    g_autofree char *client_cert = NULL;
    g_autofree char *client_key = NULL;
    g_autoptr (GList) auth_items = NULL;

    settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_insert (settings, g_strdup ("SSLClientAuth"), g_strdup ("rhsm"));
    g_hash_table_insert (settings, g_strdup ("HTTPAuth"), g_strdup ("rhn-username:rhn-password"));
    g_hash_table_insert (settings, g_strdup ("AuthDataItems"), g_strdup ("hostname, type"));

    setenv ("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH", TESTING_CERTS_CORRECT_DIR_PATH, 1);

    config = ureport_server_config_new_for_settings (settings);
    username = ureport_server_config_get_username (config);
    password = ureport_server_config_get_password (config);
    client_cert = ureport_server_config_get_client_cert (config);
    client_key = ureport_server_config_get_client_key (config);
    auth_items = ureport_server_config_get_auth_items (config);

    unsetenv ("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH");

    g_assert_cmpstr (username, ==, "rhn-username");
    g_assert_cmpstr (password, ==, "rhn-password");
    g_assert_null (client_cert);
    g_assert_null (client_key);
    g_assert_cmpstr (auth_items->data, ==, "hostname");
    g_assert_cmpstr (auth_items->next->data, ==, "type");
}

void
test_new_for_settings_environment_1 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *url = NULL;
    bool ssl_verify;
    g_autoptr (GList) auth_items = NULL;

    setenv ("uReport_URL", "env_url", 1);
    setenv ("uReport_SSLVerify", "yes", 1);
    setenv ("uReport_SSLClientAuth", "", 1);
    setenv ("uReport_IncludeAuthData", "no", 1);
    setenv ("uReport_AuthDataItems", "hostname", 1);

    settings = g_hash_table_new (NULL, NULL);
    config = ureport_server_config_new_for_settings (settings);
    url = ureport_server_config_get_url (config);
    ssl_verify = ureport_server_config_get_ssl_verify (config);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_cmpstr (url, ==, "env_url");
    g_assert_true (ssl_verify);
    g_assert_null (auth_items);

    unsetenv ("uReport_URL");
    unsetenv ("uReport_SSLVerify");
    unsetenv ("uReport_SSLClientAuth");
    unsetenv ("uReport_IncludeAuthData");
    unsetenv ("uReport_AuthDataItems");
}

/* IncludeAuthData set, but AuthDataItems not. */
void
test_new_for_settings_environment_2 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *url = NULL;
    bool ssl_verify;
    g_autoptr (GList) auth_items = NULL;

    setenv ("uReport_URL", "env_url", 1);
    setenv ("uReport_SSLVerify", "yes", 1);
    setenv ("uReport_SSLClientAuth", "", 1);
    setenv ("uReport_IncludeAuthData", "yes", 1);
    setenv ("uReport_AuthDataItems", "", 1);

    settings = g_hash_table_new (NULL, NULL);
    config = ureport_server_config_new_for_settings (settings);
    url = ureport_server_config_get_url (config);
    ssl_verify = ureport_server_config_get_ssl_verify (config);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_cmpstr (url, ==, "env_url");
    g_assert_true (ssl_verify);
    g_assert_null(auth_items);

    unsetenv ("uReport_URL");
    unsetenv ("uReport_SSLVerify");
    unsetenv ("uReport_SSLClientAuth");
    unsetenv ("uReport_IncludeAuthData");
    unsetenv ("uReport_AuthDataItems");
}

void
test_new_for_settings_environment_3 (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *url = NULL;
    bool ssl_verify;
    g_autoptr (GList) auth_items = NULL;

    setenv ("uReport_URL", "env_url", 1);
    setenv ("uReport_SSLVerify", "no", 1);
    setenv ("uReport_SSLClientAuth", "", 1);
    setenv ("uReport_IncludeAuthData", "yes", 1);
    setenv ("uReport_AuthDataItems", "hostname, time", 1);

    settings = g_hash_table_new (NULL, NULL);
    config = ureport_server_config_new_for_settings (settings);
    url = ureport_server_config_get_url (config);
    ssl_verify = ureport_server_config_get_ssl_verify (config);
    auth_items = ureport_server_config_get_auth_items (config);

    g_assert_cmpstr(url, ==, "env_url");
    g_assert_false (ssl_verify);
    g_assert_cmpstr (auth_items->data, ==, "hostname");
    g_assert_cmpstr(auth_items->next->data, ==, "time");
}

void
test_new_for_settings_environment_basic_auth (void)
{
    g_autoptr (GHashTable) settings = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;
    g_autofree char *client_cert = NULL;
    g_autofree char *client_key = NULL;
    g_autoptr (GList) auth_items = NULL;

    setenv ("uReport_SSLClientAuth", "rhsm", 1);
    setenv ("uReport_HTTPAuth", "username:password", 1);
    setenv ("uReport_AuthDataItems", "hostname, time", 1);

    setenv ("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH", TESTING_CERTS_CORRECT_DIR_PATH, 1);

    settings = g_hash_table_new (NULL, NULL);
    config = ureport_server_config_new_for_settings (settings);
    username = ureport_server_config_get_username (config);
    password = ureport_server_config_get_password (config);
    client_cert = ureport_server_config_get_client_cert (config);
    client_key = ureport_server_config_get_client_key (config);
    auth_items = ureport_server_config_get_auth_items (config);

    unsetenv("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH");

    unsetenv("uReport_SSLClientAuth");
    unsetenv("uReport_HTTPAuth");
    unsetenv("uReport_AuthDataItems");

    g_assert_cmpstr (username, ==, "username");
    g_assert_cmpstr (password, ==, "password");
    g_assert_null (client_cert);
    g_assert_null (client_key);
    g_assert_cmpstr (auth_items->data, ==, "hostname");
    g_assert_cmpstr (auth_items->next->data, ==, "time");
}

void
test_set_client_auth_1 (void)
{
    g_autoptr (UReportServerConfig) config = NULL;

    config = ureport_server_config_new ();

    setenv ("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH", TESTING_CERTS_CORRECT_DIR_PATH, 1);

    ureport_server_config_set_client_auth (config, "rhsm");

    unsetenv ("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH");

    assert_ureport_server_config (config, RHSM_WEB_SERVICE_URL, true,
                                  TESTING_CERTS_CORRECT_DIR_PATH "/cert.pem",
                                  TESTING_CERTS_CORRECT_DIR_PATH "/key.pem",
                                  NULL, NULL);
}

void
test_set_client_auth_2 (void)
{
    g_autoptr (UReportServerConfig) config = NULL;

    config = ureport_server_config_new ();

    ureport_server_config_set_basic_auth (config, "username", "password");
    ureport_server_config_set_client_auth (config, "cert:key");

    assert_ureport_server_config (config, NULL, true, "cert", "key", NULL, NULL);
}

void
test_set_client_auth_3 (const void *data)
{
    if (g_test_subprocess ())
    {
        g_autoptr (UReportServerConfig) config = NULL;

        config = ureport_server_config_new ();

        ureport_server_config_set_client_auth (config, data);
    }

    g_test_trap_subprocess (NULL, 0, 0);
    g_test_trap_assert_failed ();
}

void
test_set_client_auth_4 (void)
{
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *cert_path = NULL;
    g_autofree char *key_path = NULL;

    config = ureport_server_config_new ();
    cert_path = g_canonicalize_filename (TESTING_CERTS_CORRECT_DIR_PATH "/cert.pem", NULL);
    key_path = g_canonicalize_filename (TESTING_CERTS_CORRECT_DIR_PATH "/key.pem", NULL);

    setenv ("PYTHONPATH", TESTING_PYTHONPATH, 1);

    ureport_server_config_set_client_auth (config, "rhsm");

    unsetenv ("PYTHONPATH");

    assert_ureport_server_config (config, RHSM_WEB_SERVICE_URL, true,
                                  cert_path, key_path,
                                  NULL, NULL);
}

void
test_set_client_auth_5_1 (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (UReportServerConfig) config = NULL;

        config = ureport_server_config_new ();

        setenv("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH", RHSMCON_PEM_DIR_PATH, 1);

        ureport_server_config_set_client_auth (config, "rhsm");

        unsetenv("LIBREPORT_DEBUG_RHSMCON_PEM_DIR_PATH");

        assert_ureport_server_config (config, NULL, true, NULL, NULL, NULL, NULL);

        return;
    }

    g_test_trap_subprocess (NULL, 0, 0);
    g_test_trap_assert_failed ();
}

void
test_set_client_auth_5_2 (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (UReportServerConfig) config = NULL;

        config = ureport_server_config_new ();

        setenv("PYTHONPATH", WRONG_TESTING_PYTHONPATH, 1);

        ureport_server_config_set_client_auth (config, "rhsm");

        unsetenv("PYTHONPATH");

        assert_ureport_server_config (config, NULL, true, NULL, NULL, NULL, NULL);

        return;
    }

    g_test_trap_subprocess (NULL, 0, 0);
    g_test_trap_assert_failed ();
}

void
test_set_basic_auth (const void *data)
{
    const char **credentials;
    g_autoptr (UReportServerConfig) config = NULL;

    credentials = (const char **) data;
    config = ureport_server_config_new ();

    ureport_server_config_set_basic_auth (config, credentials[0], credentials[1]);

    assert_ureport_server_config (config, NULL, true, NULL, NULL,
                                  credentials[0], credentials[1]);
}

void
test_server_response_new_from_reply_1 (const void *data)
{
    UReportServerConfig *config;
    struct post_state *state;
    g_autoptr (UReportServerResponse) response = NULL;

    config = (UReportServerConfig *) data;
    state = new_post_state (0);

    state->curl_result = 1;
    state->body = g_strdup ("body");

    strcpy (state->errmsg,
            "Artificial Error for the purpose of testing ability to recover from errors");

    response = ureport_server_response_new_from_reply (state, config);

    free_post_state (state);

    g_assert_null (response);
}

void
test_server_response_new_from_reply_2 (const void *data)
{
    UReportServerConfig *config;

    config = (UReportServerConfig *) data;

    int response_codes[] =
    {
        201,
        202,
        404,
        500,
        503,
    };
    struct post_state *state;

    state = new_post_state (0);

    state->body = g_strdup ("body");

    for (size_t i = 0; i < G_N_ELEMENTS (response_codes); i++)
    {
        g_autoptr (UReportServerResponse) response = NULL;

        state->http_resp_code = response_codes[i];

        response = ureport_server_response_new_from_reply (state, config);

        g_assert_null (response);
    }

    free_post_state (state);
}

void
test_server_response_new_from_reply_3 (const void *data)
{
    UReportServerConfig *config;
    struct post_state *state;
    g_autoptr (UReportServerResponse) response = NULL;

    config = (UReportServerConfig *) data;
    state = new_post_state (0);

    state->http_resp_code = 202;
    state->body = g_strdup ("{ \"resultxxxxxxxx\" : true }");

    response = ureport_server_response_new_from_reply (state, config);

    free_post_state (state);

    g_assert_null (response);
}

void
test_server_response_new_from_reply_4 (const void *data)
{
    UReportServerConfig *config;
    struct post_state *state;
    g_autoptr (UReportServerResponse) response = NULL;
    g_autofree char *value = NULL;
    g_autofree char *message = NULL;
    g_autofree char *bthash = NULL;
    g_autoptr (GList) reported_to_list = NULL;

    config = (UReportServerConfig *) data;
    state = new_post_state (0);

    state->http_resp_code = 202;
    state->body = g_strdup ("{\
                                \"result\" : true,\
                                \"message\": \"message\",\
                                \"bthash\": \"691cf824e3e07457156125636e86c50279e29496\",\
                                \"reported_to\": [\
                                    {\
                                        \"type\": \"url\",\
                                        \"value\": \"value\",\
                                        \"reporter\": \"ABRT Server\"\
                                    }\
                                ]\
                            }");

    response = ureport_server_response_new_from_reply (state, config);
    value = ureport_server_response_get_value (response);
    message = ureport_server_response_get_message (response);
    bthash = ureport_server_response_get_bthash (response);
    reported_to_list = ureport_server_response_get_reported_to_list (response);

    g_assert_cmpstr (value, ==, "true");
    g_assert_cmpstr (message, ==, "message");
    g_assert_cmpstr (bthash, ==, "691cf824e3e07457156125636e86c50279e29496");
    g_assert_cmpstr (reported_to_list->data, ==, "ABRT Server: URL=value");
}

void
test_server_response_get_report_url (void)
{
    struct post_state *post_state;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autoptr (UReportServerResponse) response = NULL;
    g_autofree char *report_url = NULL;
    g_autofree char *url = NULL;
    g_autofree char *bthash = NULL;
    g_autofree char *expected_url = NULL;

    post_state = new_post_state (0);

    post_state->curl_result = CURLE_OK;
    post_state->http_resp_code = 202;
    post_state->body = g_strdup ("{\
                                     \"result\": true,\
                                     \"message\": 'message', \
                                     \"bthash\": '691cf824e3e07457156125636e86c50279e29496', \
                                     \"reported_to\": [ { 'type': 'url', \
                                     \"value\": 'value', \
                                     \"reporter\": 'ABRT Server' } ] }");

    config = ureport_server_config_new ();

    ureport_server_config_set_url (config, "url");

    response = ureport_server_response_new_from_reply (post_state, config);
    report_url = ureport_server_response_get_report_url (response, config);
    url = ureport_server_config_get_url (config);
    bthash = ureport_server_response_get_bthash (response);
    expected_url = g_build_path (G_DIR_SEPARATOR_S, url, BTHASH_URL_SUFFIX, bthash, NULL);

    g_assert_cmpstr (report_url, ==, expected_url);
}

void
test_do_post (void)
{
    struct dump_dir *dd;
    g_autofree char *json = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    struct post_state *post_state;

    dd = dd_create ("./test", (uid_t) -1L, DEFAULT_DUMP_DIR_MODE);

    g_assert_nonnull (dd);

    dd_create_basic_files (dd, (uid_t) -1L, NULL);

    dd_save_text (dd, FILENAME_TYPE, "CCpp");
    dd_save_text (dd, FILENAME_ANALYZER, "CCpp");
    dd_save_text (dd, FILENAME_PKG_EPOCH, "pkg_epoch");
    dd_save_text (dd, FILENAME_PKG_ARCH, "pkg_arch");
    dd_save_text (dd, FILENAME_PKG_RELEASE, "pkg_release");
    dd_save_text (dd, FILENAME_PKG_VERSION, "pkg_version");
    dd_save_text (dd, FILENAME_PKG_NAME, "pkg_name");
    dd_save_text (dd, FILENAME_CORE_BACKTRACE,
                  "{ \"signal\": 6, \"executable\": \"/usr/bin/will_abort\" }");
    dd_save_text (dd, FILENAME_COUNT, "1");

    dd_close(dd);

    json = ureport_from_dump_dir ("./test", NULL, 0);
    config = ureport_server_config_new ();

    ureport_server_config_set_url (config, "");

    post_state = ureport_do_post (json, config, "not_exist");

    g_assert_cmpint (post_state->curl_result, ==, CURLE_COULDNT_RESOLVE_HOST);

    free_post_state (post_state);

    delete_dump_dir ("./test");
}

void
test_submit (void)
{
    struct dump_dir *dd;
    g_autofree char *json = NULL;
    g_autoptr (UReportServerConfig) config = NULL;
    g_autoptr (UReportServerResponse) response = NULL;

    dd = dd_create("./test", (uid_t) -1L, DEFAULT_DUMP_DIR_MODE);

    g_assert_nonnull (dd);

    dd_create_basic_files (dd, (uid_t) -1L, NULL);

    dd_save_text (dd, FILENAME_TYPE, "CCpp");
    dd_save_text (dd, FILENAME_ANALYZER, "CCpp");
    dd_save_text (dd, FILENAME_PKG_EPOCH, "pkg_epoch");
    dd_save_text (dd, FILENAME_PKG_ARCH, "pkg_arch");
    dd_save_text (dd, FILENAME_PKG_RELEASE, "pkg_release");
    dd_save_text (dd, FILENAME_PKG_VERSION, "pkg_version");
    dd_save_text (dd, FILENAME_PKG_NAME, "pkg_name");
    dd_save_text (dd, FILENAME_CORE_BACKTRACE,
                  "{ \"signal\": 6, \"executable\": \"/usr/bin/will_abort\" }");
    dd_save_text (dd, FILENAME_COUNT, "1");

    dd_close (dd);

    json = ureport_from_dump_dir ("./test", NULL, 0);
    config = ureport_server_config_new ();
    response = ureport_submit (json, config);

    g_assert_null (response);

    delete_dump_dir("./test");
}

void
test_server_response_save_in_dump_dir_1 (void)
{
    struct post_state *post_state;
    g_autoptr (UReportServerConfig) config = NULL;
    struct dump_dir *dd;
    g_autoptr (UReportServerResponse) response = NULL;
    bool saved;
    g_autofree char *reported_to = NULL;
    char *needle;

    post_state = new_post_state (0);
    config = ureport_server_config_new ();

    post_state->curl_result = CURLE_OK;
    post_state->http_resp_code = 202;
    post_state->body = g_strdup ("{\
                                     \"result\": true,\
                                     \"message\": \"message\",\
                                     \"bthash\": \"691cf824e3e07457156125636e86c50279e29496\",\
                                     \"reported_to\": [\
                                         {\
                                             \"type\": \"url\",\
                                             \"value\": \"value\",\
                                             \"reporter\": \"ABRT Server\"\
                                         }\
                                     ]\
                                  }");

    ureport_server_config_set_url (config, "url");

    dd = dd_create("./test", (uid_t) -1L, DEFAULT_DUMP_DIR_MODE);

    g_assert_nonnull (dd);

    dd_create_basic_files (dd, (uid_t) -1L, NULL);

    dd_save_text (dd, FILENAME_TYPE, "CCpp");
    dd_save_text (dd, FILENAME_COUNT, "1");

    dd_close (dd);

    response = ureport_server_response_new_from_reply (post_state, config);

    saved = ureport_server_response_save_in_dump_dir (response, "./test", config);
    g_assert_true (saved);

    saved = ureport_server_response_save_in_dump_dir (response, "not_existing_dir", config);
    g_assert_false (saved);

    dd = dd_opendir ("./test", 0);
    reported_to = dd_load_text_ext (dd, FILENAME_REPORTED_TO, DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);

    needle = strstr (reported_to, "uReport: BTHASH=691cf824e3e07457156125636e86c50279e29496");
    g_assert_nonnull (needle);

    needle = strstr (reported_to, "url/reports/bthash/691cf824e3e07457156125636e86c50279e29496");
    g_assert_nonnull (needle);

    needle = strstr (reported_to, "ABRT Server: URL=value");
    g_assert_nonnull (needle);

    g_assert_false (dd_exist(dd, FILENAME_NOT_REPORTABLE));

    dd_close(dd);

    delete_dump_dir("./test");
}

void
test_server_response_save_in_dump_dir_2 (void)
{
    struct post_state *post_state;
    g_autoptr (UReportServerConfig) config = NULL;
    struct dump_dir *dd;
    g_autoptr (UReportServerResponse) response = NULL;
    bool saved;
    g_autofree char *reported_to = NULL;
    char *needle;
    g_autofree char *not_reportable = NULL;

    post_state = new_post_state (0);
    config = ureport_server_config_new ();

    post_state->curl_result = CURLE_OK;
    post_state->http_resp_code = 202;
    post_state->body = g_strdup ("{\
                                     \"result\": true,\
                                     \"message\": \"message\",\
                                     \"solutions\": [\
                                         {\
                                             \"cause\": \"solution_cause\",\
                                             \"url\": \"solution_url\",\
                                             \"note\": \"solution_note\"\
                                         }\
                                     ],\
                                     \"bthash\": \"691cf824e3e07457156125636e86c50279e29496\",\
                                     \"reported_to\": [\
                                         {\
                                             \"type\": \"url\",\
                                             \"value\": \"value\",\
                                             \"reporter\": \"ABRT Server\"\
                                         }\
                                     ]\
                                  }");

    ureport_server_config_set_url (config, "url");

    dd = dd_create("./test", (uid_t)-1L, DEFAULT_DUMP_DIR_MODE);

    g_assert_nonnull (dd);

    dd_create_basic_files (dd, (uid_t) -1L, NULL);

    dd_save_text (dd, FILENAME_TYPE, "CCpp");
    dd_save_text (dd, FILENAME_COUNT, "1");

    dd_close (dd);

    response = ureport_server_response_new_from_reply (post_state, config);
    saved = ureport_server_response_save_in_dump_dir (response, "./test", config);

    g_assert_true (saved);

    dd = dd_opendir ("./test", 0);

    reported_to = dd_load_text_ext (dd, FILENAME_REPORTED_TO, DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);

    needle = strstr (reported_to, "uReport: BTHASH=691cf824e3e07457156125636e86c50279e29496");
    g_assert_nonnull (needle);

    needle = strstr (reported_to, "url/reports/bthash/691cf824e3e07457156125636e86c50279e29496");
    g_assert_nonnull (needle);

    needle = strstr (reported_to, "ABRT Server: URL=value");
    g_assert_nonnull (needle);

    not_reportable = dd_load_text_ext (dd, FILENAME_NOT_REPORTABLE, DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE);

    needle = strstr (not_reportable, "Your problem seems to be caused by solution_cause");
    g_assert_nonnull (needle);

    dd_close (dd);

    delete_dump_dir ("./test");
}

void
test_json_attachment (void)
{
    g_autofree char *json = NULL;

    json = ureport_json_attachment_new ("bthash", "type", "data");

    g_assert_cmpstr (json,
                     ==,
                     "{ \"bthash\": \"bthash\", \"type\": \"type\", \"data\": \"data\" }");
}

void
test_attach (void)
{
    g_autoptr (UReportServerConfig) config = NULL;
    bool attached;

    config = ureport_server_config_new ();

    ureport_server_config_set_url (config, "");

    attached = ureport_attach (config,
                               "691cf824e3e07457156125636e86c50279e29496",
                               "email",
                               "%s", "abrt@email.com");

    g_assert_false (attached);

    attached = ureport_attach (config,
                               "691cf824e3e07457156125636e86c50279e29496",
                               "count",
                               "%d", 5);

    g_assert_false (attached);
}

void
test_from_dump_dir (void)
{
    struct dump_dir *dd;

    dd = dd_create ("./test", (uid_t) -1L, DEFAULT_DUMP_DIR_MODE);

    g_assert_nonnull (dd);

    dd_create_basic_files (dd, (uid_t) -1L, NULL);

    dd_save_text (dd, FILENAME_TYPE, "CCpp");
    dd_save_text (dd, FILENAME_ANALYZER, "CCpp");
    dd_save_text (dd, FILENAME_PKG_EPOCH, "pkg_epoch");
    dd_save_text (dd, FILENAME_PKG_ARCH, "pkg_arch");
    dd_save_text (dd, FILENAME_PKG_RELEASE, "pkg_release");
    dd_save_text (dd, FILENAME_PKG_VERSION, "pkg_version");
    dd_save_text (dd, FILENAME_PKG_NAME, "pkg_name");
    dd_save_text (dd, FILENAME_CORE_BACKTRACE, "{ \"signal\": 6, \"executable\": \"/usr/bin/will_abort\" }");
    dd_save_text (dd, FILENAME_COUNT, "1");

    dd_close (dd);

    {
        g_autofree char *ureport = NULL;
        char *needle;

        ureport = ureport_from_dump_dir ("./test", NULL, 0);
        needle = strstr (ureport, "auth");

        g_assert_null (needle);
    }

    dd = dd_opendir ("./test", 0);

    dd_save_text (dd, FILENAME_HOSTNAME, "env_hostname");

    dd_close (dd);

    {
        g_autoptr (GHashTable) settings = NULL;
        g_autoptr (UReportServerConfig) config = NULL;
        g_autoptr (GList) auth_items = NULL;
        UReportPreferencesFlags flags;
        g_autofree char *ureport = NULL;
        char *needle;

        setenv ("uReport_IncludeAuthData", "yes", 1);
        setenv ("uReport_AuthDataItems", "hostname", 1);

        settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        config = ureport_server_config_new_for_settings (settings);

        auth_items = ureport_server_config_get_auth_items (config);
        flags = ureport_server_config_get_flags (config);
        ureport = ureport_from_dump_dir ("./test", auth_items, flags);
        needle = strstr (ureport, "auth");

        g_assert_nonnull (needle);

        needle = strstr (ureport, "\"hostname\": \"env_hostname\"");

        g_assert_nonnull (needle);
    }

    {
        g_autoptr (GHashTable) settings = NULL;
        g_autoptr (UReportServerConfig) config = NULL;
        g_autoptr (GList) auth_items = NULL;
        UReportPreferencesFlags flags;
        g_autofree char *ureport = NULL;
        char *needle;

        setenv ("uReport_AuthDataItems", "hostname, unknown", 1);

        settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        config = ureport_server_config_new_for_settings (settings);

        auth_items = ureport_server_config_get_auth_items (config);
        flags = ureport_server_config_get_flags (config);
        ureport = ureport_from_dump_dir ("./test", auth_items, flags);
        needle = strstr (ureport, "auth");

        g_assert_nonnull (needle);

        needle = strstr (ureport, "\"hostname\": \"env_hostname\"");

        g_assert_nonnull (needle);

        needle = strstr (ureport, "unknown");

        g_assert_null (needle);
    }

    delete_dump_dir("./test");
}

void
test_server_config_load_basic_auth_1 (void)
{
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;

    config = ureport_server_config_new ();

    ureport_server_config_load_basic_auth (config, "username:password");

    username = ureport_server_config_get_username (config);
    password = ureport_server_config_get_password (config);

    g_assert_cmpstr (username, ==, "username");
    g_assert_cmpstr (password, ==, "password");
}

void
test_server_config_load_basic_auth_2 (void)
{
    g_autoptr (UReportServerConfig) config = NULL;
    g_autofree char *username = NULL;
    g_autofree char *password = NULL;
    g_autofree char *url = NULL;

    config = ureport_server_config_new ();

    setenv("LIBREPORT_DEBUG_PLUGINS_CONF_DIR", "ureport-rhts-credentials", 1);

    ureport_server_config_load_basic_auth (config, "rhts-credentials");

    unsetenv("LIBREPORT_DEBUG_PLUGINS_CONF_DIR");

    username = ureport_server_config_get_username (config);
    password = ureport_server_config_get_password (config);
    url = ureport_server_config_get_url (config);

    g_assert_cmpstr (username, ==, "rhn-user-name");
    g_assert_cmpstr (password, ==, "rhn-password");
    g_assert_cmpstr (url, ==, RHSM_WEB_SERVICE_URL);
}

void
test_server_config_load_basic_auth_3 (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (UReportServerConfig) config = NULL;

        config = ureport_server_config_new ();

        setenv ("REPORT_CLIENT_NONINTERACTIVE", "1", 1);

        ureport_server_config_load_basic_auth (config, "username");

        return;
    }

    g_test_trap_subprocess (NULL, 0, 0);
    g_test_trap_assert_failed ();
}

int
main (int    argc,
      char **argv)
{
    g_verbose = 3;

    g_test_init (&argc, &argv, NULL);

    g_test_bug_base ("https://bugzilla.redhat.com/show_bug.cgi?id=");

    g_test_add_func ("/ureport/server-config/defaults", test_defaults);

    g_test_add_func ("/ureport/server-config/new-for-settings.1",
                     test_new_for_settings_1);
    g_test_add_func ("/ureport/server-config/new-for-settings.2",
                     test_new_for_settings_2);
    g_test_add_func ("/ureport/server-config/new-for-settings-basic-auth",
                     test_new_for_settings_basic_auth);

    g_test_add_func ("/ureport/server-config/new-for-settings-environment.1",
                     test_new_for_settings_environment_1);
    g_test_add_func ("/ureport/server-config/new-for-settings-environment.2",
                     test_new_for_settings_environment_2);
    g_test_add_func ("/ureport/server-config/new-for-settings-environment.3",
                     test_new_for_settings_environment_3);
    g_test_add_func ("/ureport/server-config/new-for-settings-environment-basic-auth",
                     test_new_for_settings_environment_basic_auth);

    g_test_add_func ("/ureport/server-config/set-client-auth.1",
                     test_set_client_auth_1);
    g_test_add_func ("/ureport/server-config/set-client-auth.2",
                     test_set_client_auth_2);
    g_test_add_data_func ("/ureport/server-config/set-client-auth.3.1",
                          "cert:",
                          test_set_client_auth_3);
    g_test_add_data_func ("/ureport/server-config/set-client-auth.3.2",
                          ":key",
                          test_set_client_auth_3);
    g_test_add_data_func ("/ureport/server-config/set-client-auth.3.3",
                          "cert",
                          test_set_client_auth_3);
    g_test_add_func ("/ureport/server-config/set-client-auth.4",
                     test_set_client_auth_4);
    g_test_add_func ("/ureport/server-config/set-client-auth.5.1",
                     test_set_client_auth_5_1);
    g_test_add_func ("/ureport/server-config/set-client-auth.5.2",
                     test_set_client_auth_5_2);
    g_test_add_data_func ("/ureport/server-config/set-basic-auth.null",
                          (const char *[]) { NULL, NULL },
                          test_set_basic_auth);
    g_test_add_data_func ("/ureport/server-config/set-basic-auth.null-password",
                          (const char *[]) { "username", NULL },
                          test_set_basic_auth);
    g_test_add_data_func ("/ureport/server-config/set-basic-auth.null-username",
                          (const char *[]) { NULL, "password" },
                          test_set_basic_auth);
    g_test_add_data_func ("/ureport/server-config/set-basic-auth",
                          (const char *[]) { "username", "password" },
                          test_set_basic_auth);

    {
        g_autoptr (UReportServerConfig) config = NULL;

        config = ureport_server_config_new ();

        ureport_server_config_set_url (config, "url");

        g_test_add_data_func ("/ureport/server-response/new-from-reply.1",
                              config,
                              test_server_response_new_from_reply_1);
        g_test_add_data_func ("/ureport/server-response/new-from-reply.2",
                              config,
                              test_server_response_new_from_reply_2);
        g_test_add_data_func ("/ureport/server-response/new-from-reply.3",
                              config,
                              test_server_response_new_from_reply_3);
        g_test_add_data_func ("/ureport/server-response/new-from-reply.4",
                              config,
                              test_server_response_new_from_reply_4);
    }

    g_test_add_func ("/ureport/server-response/save-in-dump-dir.1",
                     test_server_response_save_in_dump_dir_1);
    g_test_add_func ("/ureport/server-response/save-in-dump-dir.2",
                     test_server_response_save_in_dump_dir_2);

    g_test_add_func ("/ureport/server-response/get-report-url",
                     test_server_response_get_report_url);

    g_test_add_func ("/ureport/do-post", test_do_post);

    g_test_add_func ("/ureport/submit", test_submit);

    g_test_add_func ("/ureport/json-attachment", test_json_attachment);

    g_test_add_func ("/ureport/attach", test_attach);

    g_test_add_func ("/ureport/from-dump-dir", test_from_dump_dir);

    g_test_add_func ("/ureport/server-config/load-basic-auth.1",
                     test_server_config_load_basic_auth_1);
    g_test_add_func ("/ureport/server-config/load-basic-auth.2",
                     test_server_config_load_basic_auth_2);
    g_test_add_func ("/ureport/server-config/load-basic-auth.3",
                     test_server_config_load_basic_auth_3);

    return g_test_run ();
}
