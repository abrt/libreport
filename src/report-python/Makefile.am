PYFILES = __init__.py accountmanager.py

PYEXTFILES = \
    reportmodule.c \
    problem_data.c \
    dump_dir.c \
    run_event.c \
    report.c \
    common.h

PYEXTCPPFLAGS = \
    -I$(srcdir)/../include/report -I$(srcdir)/../include \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DLOCALSTATEDIR='"$(localstatedir)"' \
    -DVAR_RUN=\"$(VAR_RUN)\" \
    -DDEFAULT_DUMP_DIR_MODE=$(DEFAULT_DUMP_DIR_MODE) \
    $(GLIB_CFLAGS) \
    -D_GNU_SOURCE \
    -fPIE

PYEXTLDFLAGS = \
    -module \
    -avoid-version \
    -Wl,-z,relro -Wl,-z,now

if BUILD_PYTHON2
pyreportexecdir = $(pyexecdir)/report

pyreportexec_PYTHON = $(PYFILES)
pyreportexec_LTLIBRARIES = _pyreport.la

_pyreport_la_SOURCES = $(PYEXTFILES)

_pyreport_la_CPPFLAGS = \
    $(PYEXTCPPFLAGS) \
    $(PYTHON_CFLAGS)

_pyreport_la_LDFLAGS = \
   $(PYEXTLDFLAGS) \
   -export-symbols-regex init_pyreport

_pyreport_la_LIBADD = \
    ../lib/libreport.la \
    $(PYTHON2_LIBS)
endif

if BUILD_PYTHON3
py3reportexecdir = $(py3execdir)/report

py3reportexec_PYTHON = $(PYFILES)
py3reportexec_LTLIBRARIES = _py3report.la

_py3report_la_SOURCES = $(PYEXTFILES)

_py3report_la_CPPFLAGS = \
    $(PYEXTCPPFLAGS) \
    $(PYTHON3_CFLAGS)

_py3report_la_LDFLAGS = \
   $(PYEXTLDFLAGS) \
   -export-symbols-regex PyInit__py3report

_py3report_la_LIBADD = \
    ../lib/libreport.la \
    $(PYTHON3_LIBS)
endif

# report compat:
PYIOFILES = \
    io/__init__.py \
    io/GTKIO.py \
    io/NewtIO.py \
    io/TextIO.py

if BUILD_PYTHON2
pyreportioexecdir = $(pyexecdir)/report/io
pyreportioexec_PYTHON = $(PYIOFILES)
endif

if BUILD_PYTHON3
py3reportioexecdir = $(py3execdir)/report/io
py3reportioexec_PYTHON = $(PYIOFILES)
endif
