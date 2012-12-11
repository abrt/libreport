# libreportgtk - the stuff shared among gtk clients (abrt-gui, wizard)
libreport_gtk_includedir = \
    $(includedir)/libreport

libreport_gtk_include_HEADERS = \
    internal_libreport_gtk.h

lib_LTLIBRARIES = \
    libreport-gtk.la

libreport_gtk_la_SOURCES = \
    event_config_dialog.c \
    secrets.c \
    hyperlinks.c \
    autowrapped_label.c \
    workflow_config_dialog.c \
    config_dialog.c \
    ask_dialogs.c

libreport_gtk_la_CPPFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    $(GTK_CFLAGS) \
    $(GLIB_CFLAGS) \
    $(GIO_CFLAGS) \
    -DWORKFLOWS_DIR=\"$(WORKFLOWS_DIR)\" \
    -D_GNU_SOURCE
libreport_gtk_la_LDFLAGS = \
    -version-info 0:1:0
libreport_gtk_la_LIBADD = \
    $(GTK_LIBS) \
    $(GLIB_LIBS) \
    $(GIO_LIBS) \
    ../lib/libreport.la

EXTRA_DIST = libreport-gtk.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libreport-gtk.pc
