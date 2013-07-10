pluginslibdir = $(PLUGINS_LIB_DIR)

reporters_bin =
if BUILD_BUGZILLA
reporters_bin += \
    reporter-bugzilla \
    report
endif

if BUILD_UREPORT
reporters_bin +=  reporter-ureport
endif

bin_PROGRAMS = $(reporters_bin) \
    reporter-rhtsupport \
    reporter-kerneloops \
    reporter-upload \
    reporter-mailx \
    reporter-print

pluginsconfdir = $(PLUGINS_CONF_DIR)

reportpluginsconfdir = $(REPORT_PLUGINS_CONF_DIR)

reporters_plugin_conf =
if BUILD_BUGZILLA
reporters_plugin_conf += bugzilla.conf \
    bugzilla_format.conf \
    bugzilla_formatdup.conf \
    bugzilla_format_libreport.conf \
    bugzilla_format_kernel.conf \
    bugzilla_format_anaconda.conf \
    bugzilla_formatdup_anaconda.conf

endif

dist_reportpluginsconf_DATA = $(reporters_plugin_conf) \
    rhtsupport.conf \
    mailx.conf

eventsdir = $(EVENTS_DIR)
eventsconfdir = $(EVENTS_CONF_DIR)

reporters_events =
reporters_events_conf =
if BUILD_BUGZILLA
reporters_events += report_Bugzilla.xml
reporters_events_conf += report_Bugzilla.conf
endif

if BUILD_UREPORT
reporters_events += report_uReport.xml
endif

dist_events_DATA = $(reporters_events) \
    report_Logger.xml \
    report_Mailx.xml \
    report_RHTSupport.xml \
    report_Kerneloops.xml \
    report_Uploader.xml \
    report_EmergencyAnalysis.xml

dist_eventsconf_DATA = $(reporters_events_conf) \
    report_Logger.conf

@INTLTOOL_XML_RULE@

eventsdefdir = $(EVENTS_DEFINITION_DIR)

dist_eventsdef_DATA = \
    mailx_event.conf \
    print_event.conf \
    rhtsupport_event.conf \
    uploader_event.conf \
    emergencyanalysis_event.conf

reporters_extra_dist =
if BUILD_BUGZILLA
dist_eventsdef_DATA += bugzilla_event.conf bugzilla_anaconda_event.conf
reporters_extra_dist += report_Bugzilla.xml.in report_Bugzilla.conf
endif

if BUILD_UREPORT
reporters_extra_dist += report_uReport.xml.in
endif

PYTHON_FILES = abrt-screencast

EXTRA_DIST = $(reporters_extra_dist) \
    $(PYTHON_FILES) \
    report_Logger.conf \
    report_Logger.xml.in \
    report_Mailx.xml.in \
    report_RHTSupport.xml.in \
    report_Kerneloops.xml.in \
    report_Uploader.xml.in \
    report_EmergencyAnalysis.xml.in

libexec_SCRIPTS = abrt-screencast

$(DESTDIR)/$(DEBUG_INFO_DIR):
	$(mkdir_p) '$@'

if BUILD_BUGZILLA
reporter_bugzilla_SOURCES = \
    reporter-bugzilla.c rhbz.c rhbz.h
reporter_bugzilla_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    $(XMLRPC_CFLAGS) $(XMLRPC_CLIENT_CFLAGS) \
    -D_GNU_SOURCE
reporter_bugzilla_LDADD = \
    $(GLIB_LIBS) \
    $(XMLRPC_LIBS) $(XMLRPC_CLIENT_LIBS) \
    ../lib/libreport-web.la \
    ../lib/libreport.la
endif

reporter_rhtsupport_SOURCES = \
    abrt_rh_support.h abrt_rh_support.c \
    reporter-rhtsupport.h \
    reporter-rhtsupport-parse.c \
    reporter-rhtsupport.c
reporter_rhtsupport_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    $(LIBXML_CFLAGS) \
    -D_GNU_SOURCE
reporter_rhtsupport_LDFLAGS = -ltar
reporter_rhtsupport_LDADD = \
    $(GLIB_LIBS) \
    $(LIBXML_LIBS) \
    ../lib/libreport-web.la \
    ../lib/libreport.la

reporter_upload_SOURCES = \
    reporter-upload.c
reporter_upload_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(CURL_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
reporter_upload_LDFLAGS = -ltar
reporter_upload_LDADD = \
    $(GLIB_LIBS) \
    ../lib/libreport-web.la \
    ../lib/libreport.la

reporter_kerneloops_SOURCES = \
    reporter-kerneloops.c
reporter_kerneloops_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
reporter_kerneloops_LDADD = \
    ../lib/libreport-web.la \
    ../lib/libreport.la

reporter_mailx_SOURCES = \
    reporter-mailx.c
reporter_mailx_CPPFLAGS = \
    -I$(srcdir)/../libreport/src/include/ -I$(srcdir)/../include \
    -I$(srcdir)/../libreport/src/lib \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
reporter_mailx_LDADD = \
    ../lib/libreport.la

reporter_print_SOURCES = \
    reporter-print.c
reporter_print_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
reporter_print_LDADD = \
    ../lib/libreport.la

if BUILD_BUGZILLA
report_SOURCES = \
    report.c
report_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DBIN_DIR=\"$(bindir)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DDEBUG_INFO_DIR=\"$(DEBUG_INFO_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(REPORT_PLUGINS_CONF_DIR)\" \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
report_LDADD = \
    ../lib/libreport.la
endif

if BUILD_UREPORT
reporter_ureport_SOURCES = \
    ureport.c
reporter_ureport_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    $(GLIB_CFLAGS) \
    $(LIBREPORT_CFLAGS) \
    -D_GNU_SOURCE
reporter_ureport_LDADD = \
    ../lib/libreport.la \
    ../lib/libreport-web.la
endif

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
