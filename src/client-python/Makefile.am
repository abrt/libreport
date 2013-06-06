clientexecdir = $(pyexecdir)/reportclient

clientexec_PYTHON = \
    __init__.py \
    debuginfo.py

clientexec_LTLIBRARIES = _reportclient.la

_reportclient_la_SOURCES = \
    clientmodule.c \
    client.c \
    common.h
_reportclient_la_CPPFLAGS = \
    -I$(srcdir)/../include/report -I$(srcdir)/../include \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    $(GLIB_CFLAGS) \
    $(PYTHON_CFLAGS) \
    -D_GNU_SOURCE \
    -fPIE
_reportclient_la_LDFLAGS = \
    -module \
    -avoid-version \
    -export-symbols-regex init_reportclient \
    -Wl,-z,relro -Wl,-z,now
_reportclient_la_LIBADD = \
    ../lib/libreport.la
