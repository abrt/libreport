PYFILES = \
    __init__.py \
    debuginfo.py \
    dnfdebuginfo.py \
    yumdebuginfo.py

PYEXTFILES = \
    clientmodule.c \
    client.c \
    common.h

PYEXTCPPFLAGS = \
    -I$(srcdir)/../include/report -I$(srcdir)/../../include \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    $(GLIB_CFLAGS) \
    -D_GNU_SOURCE \
    -fPIE

PYEXTLDFLAGS = \
    -module \
    -avoid-version \
    -Wl,-z,relro -Wl,-z,now

if BUILD_PYTHON2
py2clientdir = $(pyexecdir)/reportclient

py2client_PYTHON = $(PYFILES)
py2client_LTLIBRARIES = _reportclient.la

_reportclient_la_SOURCES = $(PYEXTFILES)
_reportclient_la_CPPFLAGS = $(PYEXTCPPFLAGS) $(PYTHON_CFLAGS)
_reportclient_la_LDFLAGS = $(PYEXTLDFLAGS) \
    -export-symbols-regex init_reportclient
_reportclient_la_LIBADD = ../../lib/libreport.la
endif

if BUILD_PYTHON3
py3clientdir = $(py3execdir)/reportclient

py3client_PYTHON = $(PYFILES)
py3client_LTLIBRARIES = _reportclient3.la

_reportclient3_la_SOURCES = $(PYEXTFILES)
_reportclient3_la_CPPFLAGS = $(PYEXTCPPFLAGS) $(PYTHON3_CFLAGS)
_reportclient3_la_LDFLAGS = $(PYEXTLDFLAGS) \
    -export-symbols-regex PyInit__reportclient3
_reportclient3_la_LIBADD = ../../lib/libreport.la
endif

_reportclient3.so: $(clientexec_LTLIBRARIES)
	ln -sf $(abs_builddir)/.libs/$@ ./

all-local: _reportclient3.so

clean-local:
	rm -f _reportclient3.so
