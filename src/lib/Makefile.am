# libreport - the stuff shared among most of abrt (like xmalloc, logging)
lib_LTLIBRARIES = \
    libreport.la \
    libreport-web.la \
    libabrt_dbus.la

# Not used just yet:
# time.cpp
# xconnect.cpp

libreport_la_SOURCES = \
    xfuncs.c \
    is_in_string_list.c \
    is_in_comma_separated_list.c \
    encbase64.c \
    binhex.c \
    stdio_helpers.c \
    hash_sha1.c \
    read_write.c \
    logging.c \
    copyfd.c \
    copy_file_recursive.c \
    concat_path_file.c \
    append_to_malloced_string.c \
    overlapping_strcpy.c \
    skip_whitespace.c \
    glib_support.c \
    iso_date_string.c \
    strbuf.c \
    xatonum.c \
    spawn.c \
    dirsize.c \
    dump_dir.c \
    reported_to.c \
    abrt_sock.c \
    get_cmdline.c \
    configuration_files.c \
    make_descr.c \
    run_event.c \
    problem_data.c \
    create_dump_dir.c \
    abrt_types.c \
    parse_release.c \
    parse_options.c \
    steal_directory.c \
    event_xml_parser.c \
    event_config.c \
    report.c \
    user_settings.c \
    client.c \
    utf8.c \
    file_list.c \
    file_obj.c \
    workflow.c \
    workflow_xml_parser.c \
    config_item_info.c \
    xml_parser.c \
    libreport_init.c \
    reporters.c

libreport_la_CPPFLAGS = \
    -I$(srcdir)/../include \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DVAR_RUN=\"$(VAR_RUN)\" \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DEVENTS_DIR=\"$(EVENTS_DIR)\" \
    -DEVENTS_CONF_DIR=\"$(EVENTS_CONF_DIR)\" \
    -DWORKFLOWS_DIR=\"$(WORKFLOWS_DIR)\" \
    -DBIN_DIR=\"$(bindir)\" \
    -DDEFAULT_DUMP_DIR_MODE=$(DEFAULT_DUMP_DIR_MODE) \
    -DDUMP_DIR_OWNED_BY_USER=$(DUMP_DIR_OWNED_BY_USER) \
    -DLARGE_DATA_TMP_DIR=\"$(LARGE_DATA_TMP_DIR)\" \
    $(JSON_C_CFLAGS) \
    $(GLIB_CFLAGS) \
    $(GOBJECT_CFLAGS) \
    $(AUGEAS_CFLAGS) \
    -D_GNU_SOURCE
libreport_la_LDFLAGS = \
    -version-info 0:1:0
libreport_la_LIBADD = \
    $(JSON_C_LIBS) \
    $(GLIB_LIBS) \
    $(JOURNAL_LIBS) \
    $(GOBJECT_LIBS) \
    $(AUGEAS_LIBS)

libreportconfdir = $(CONF_DIR)
dist_libreportconf_DATA = \
    report_event.conf

libabrt_dbus_la_SOURCES = \
    abrt_dbus.c
libabrt_dbus_la_CPPFLAGS = \
    -I$(srcdir)/../include/report -I$(srcdir)/../include \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DEVENTS_DIR=\"$(EVENTS_DIR)\" \
    $(GLIB_CFLAGS) \
    $(DBUS_CFLAGS) \
    -D_GNU_SOURCE
libabrt_dbus_la_LDFLAGS = \
    -version-info 0:1:0
libabrt_dbus_la_LIBADD = \
    libreport.la \
    $(GLIB_LIBS) \
    $(DBUS_LIBS)

libreport_web_o =
if BUILD_BUGZILLA
libreport_web_o += abrt_xmlrpc.h abrt_xmlrpc.c
endif

if BUILD_UREPORT
libreport_web_o += ureport.c
endif

libreport_web_la_SOURCES = $(libreport_web_o) \
    curl.c \
    proxies.h proxies.c

libreport_web_la_CPPFLAGS = \
    -I$(srcdir)/../include \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DEVENTS_DIR=\"$(EVENTS_DIR)\" \
    $(GLIB_CFLAGS) \
    $(CURL_CFLAGS) \
    $(PROXY_CFLAGS) \
    $(LIBXML_CFLAGS) \
    $(XMLRPC_CFLAGS) $(XMLRPC_CLIENT_CFLAGS) \
    $(JSON_C_CFLAGS) \
    -D_GNU_SOURCE
libreport_web_la_LDFLAGS = \
    -version-info 0:1:0
libreport_web_la_LIBADD = \
    $(GLIB_LIBS) \
    $(CURL_LIBS) \
    $(PROXY_LIBS) \
    $(LIBXML_LIBS) \
    $(JSON_C_LIBS) \
    $(XMLRPC_LIBS) $(XMLRPC_CLIENT_LIBS) \
    libreport.la

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

$(DESTDIR)/$(DEBUG_DUMPS_DIR):
	$(mkdir_p) '$@'
# no need to chmod it here
#chmod 1777 '$@'
install-data-local: $(DESTDIR)/$(DEBUG_DUMPS_DIR)
