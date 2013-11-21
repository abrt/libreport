bin_PROGRAMS = \
    report-cli

report_cli_SOURCES = \
    cli.c \
    run-command.h run-command.c \
    cli-report.h cli-report.c
report_cli_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DLARGE_DATA_TMP_DIR=\"$(LARGE_DATA_TMP_DIR)\" \
    -DWORKFLOWS_DIR=\"$(WORKFLOWS_DIR)\" \
    $(GLIB_CFLAGS) \
    -D_GNU_SOURCE \
    $(LIBREPORT_CFLAGS)
#    $(GTK_CFLAGS)
report_cli_LDADD = \
    ../lib/libreport.la \
    $(GLIB_LIBS)
PYTHON_FILES = \
    abrt-action-install-debuginfo \
    abrt-action-list-dsos.py \
    abrt-action-analyze-core.py

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
