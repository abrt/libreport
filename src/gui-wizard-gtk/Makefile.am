# report-gtk binary
bin_PROGRAMS = report-gtk

report_gtk_SOURCES = \
    wizard.h wizard.c \
    main.c \
    wizard_glade.c

# Required for gtk_builder_connect_signals() to work correctly:
# -g
# -Wl,--export-dynamic
report_gtk_CFLAGS = \
    -I$(srcdir)/../include \
    -I$(srcdir)/../lib \
    -I$(srcdir)/../gtk-helpers \
    -DBIN_DIR=\"$(bindir)\" \
    -DCONF_DIR=\"$(CONF_DIR)\" \
    -DDATA_DIR=\"$(datadir)\" \
    -DDEBUG_DUMPS_DIR=\"$(DEBUG_DUMPS_DIR)\" \
    -DPLUGINS_LIB_DIR=\"$(PLUGINS_LIB_DIR)\" \
    -DPLUGINS_CONF_DIR=\"$(PLUGINS_CONF_DIR)\" \
    -DICON_DIR=\"${datadir}/abrt/icons/hicolor/48x48/status\" \
    $(GLIB_CFLAGS) \
    $(GTK_CFLAGS) \
    -D_GNU_SOURCE \
    -Wall -Wwrite-strings -Werror

# Required for gtk_builder_connect_signals() to work correctly:
# -lgmodule-2.0
# -lgthread-2.0
report_gtk_LDADD = \
    ../lib/libreport.la \
    ../gtk-helpers/libreport-gtk.la \
    $(GLIB_LIBS) \
    $(GTK_LIBS)

# we don't want to install it, just make it part of tarball
# created by make dist
GLADE_FILES = wizard.glade
#pkgdata_DATA = $(GLADE_FILES)
EXTRA_DIST = $(GLADE_FILES)


# For internal glade file storage in the binary:
wizard.c: wizard_glade.c

wizard_glade.c: wizard.glade
	{ \
	  echo '#define WIZARD_GLADE_CONTENTS "\'; \
	  cat $(srcdir)/wizard.glade | sed -e 's/"/\\"/g' -e 's/?/\\?/g' -e 's/$$/\\/g'; \
	  echo '"'; \
	} >wizard_glade.c


DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

@INTLTOOL_DESKTOP_RULE@
