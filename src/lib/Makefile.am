# libreport - the stuff shared among most of abrt (like xmalloc, logging)

confdir = $(CONF_DIR)
dist_conf_DATA = libreport.conf

defaultconfdir = $(DEFAULT_CONF_DIR)
dist_defaultconf_DATA = $(dist_conf_DATA)

lib_LTLIBRARIES = \
    libreport.la \
    libreport-web.la

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
    read_write.c \
    logging.c \
    copyfd.c \
    copy_file_recursive.c \
    compress.c \
    concat_path_file.c \
    append_to_malloced_string.c \
    overlapping_strcpy.c \
    skip_whitespace.c \
    glib_support.c \
    iso_date_string.c \
    strbuf.c \
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
    problem_report.c \
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
    reporters.c \
    global_configuration.c \
    uriparser.c \
    report_result.c \
    libreport.sym

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
    $(GIO_CFLAGS) \
    $(GLIB_CFLAGS) \
    $(LIBARCHIVE_CFLAGS) \
    $(GOBJECT_CFLAGS) \
    $(AUGEAS_CFLAGS) \
    $(NETTLE_CFLAGS) \
    $(SATYR_CFLAGS) \
    -D_GNU_SOURCE
libreport_la_LDFLAGS = \
    -ltar \
    -version-info 0:1:0

if HAVE_LD_VERSION_SCRIPT
libreport_la_LDFLAGS += -Wl,--version-script=$(srcdir)/libreport.sym
endif

libreport_la_LIBADD = \
    $(GIO_LIBS) \
    $(GLIB_LIBS) \
    $(LIBARCHIVE_LIBS) \
    $(JOURNAL_LIBS) \
    $(GOBJECT_LIBS) \
    $(AUGEAS_LIBS) \
    $(NETTLE_LIBS) \
    $(SATYR_LIBS)

libreportconfdir = $(CONF_DIR)
dist_libreportconf_DATA = \
    report_event.conf

libreport_web_o =
if BUILD_BUGZILLA
libreport_web_o += abrt_xmlrpc.h abrt_xmlrpc.c
endif

if BUILD_UREPORT
libreport_web_o += ureport.c
endif

libreport_web_la_SOURCES = $(libreport_web_o) \
    curl.c \
    proxies.h proxies.c \
    libreport-web.sym

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
    $(SATYR_CFLAGS) \
    -D_GNU_SOURCE
libreport_web_la_LDFLAGS = \
    -version-info 0:1:0

if HAVE_LD_VERSION_SCRIPT
libreport_web_la_LDFLAGS += -Wl,--version-script=$(srcdir)/libreport-web.sym
endif

libreport_web_la_LIBADD = \
    $(GLIB_LIBS) \
    $(CURL_LIBS) \
    $(PROXY_LIBS) \
    $(LIBXML_LIBS) \
    $(JSON_C_LIBS) \
    $(SATYR_LIBS) \
    $(XMLRPC_LIBS) $(XMLRPC_CLIENT_LIBS) \
    libreport.la

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

$(DESTDIR)/$(DEBUG_DUMPS_DIR):
	$(mkdir_p) '$@'
# no need to chmod it here
#chmod 1777 '$@'
install-data-local: $(DESTDIR)/$(DEBUG_DUMPS_DIR)
