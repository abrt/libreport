bin_PROGRAMS = report-newt

report_newt_SOURCES = \
    report-newt.c

report_newt_LDADD = \
    ../lib/libreport.la \
    $(GLIB_LIBS) \
    $(NEWT_LIBS)

report_newt_CFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    $(GLIB_CFLAGS) \
    $(NEWT_CFLAGS)

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@
