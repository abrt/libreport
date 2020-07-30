## ------------ ##
## package.m4.  ##
## ------------ ##

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(abs_top_srcdir)/src/include
AM_LDFLAGS = \
	$(GLIB_LIBS)
LDADD = \
	$(abs_top_builddir)/src/lib/libreport.la

compress_SOURCES = compress.c
check_PROGRAMS = \
	compress

TESTS = $(check_PROGRAMS)

EXTRA_DIST = \
	data/compressed-file \
	data/compressed-file.lz4 \
	data/compressed-file.xz \
	data/compressed-file.zst

package.m4: Makefile.in
	{ \
             echo '# Signature of the current package.' && \
             echo 'm4_define([AT_PACKAGE_NAME],' && \
             echo '  [@PACKAGE_NAME@])' && \
             echo 'm4_define([AT_PACKAGE_TARNAME],' && \
             echo '  [@PACKAGE_TARNAME@])' && \
             echo 'm4_define([AT_PACKAGE_VERSION],' && \
             echo '  [@PACKAGE_VERSION@])' && \
             echo 'm4_define([AT_PACKAGE_STRING],' && \
             echo '  [@PACKAGE_STRING@])' && \
             echo 'm4_define([AT_PACKAGE_BUGREPORT],' && \
             echo '  [@PACKAGE_BUGREPORT@])'; \
             echo 'm4_define([AT_PACKAGE_URL],' && \
             echo '  [@PACKAGE_URL@])'; \
	} >'package.m4'
EXTRA_DIST += package.m4

## ------------ ##
## Test suite.  ##
## ------------ ##

libreport_include_helpersdir = $(includedir)/libreport/helpers
libreport_include_helpers_HEADERS = \
	helpers/testsuite.h \
	helpers/testsuite_tools.h

TESTSUITEFLAGS = \
    CPPFLAGS='-DCONF_DIR="$(CONF_DIR)" $(CPPFLAGS)'

TESTSUITE_AT = \
  local.at \
  testsuite.at \
  strbuf.at \
  osrelease.at \
  osinfo.at \
  is_text_file.at \
  load_rule_list.at \
  taghyperlinks.at \
  glib_helpers.at \
  sitem.at \
  configuration_files.at \
  report_result.at \
  reported_to.at \
  problem_data.at \
  make_description.at \
  libreport_types.at \
  xml_definition.at \
  report_python.at \
  client_python.at \
  xfuncs.at \
  string_list.at \
  ureport.at \
  problem_report.at \
  dump_dir.at \
  global_config.at \
  iso_date.at \
  uriparser.at \
  event_config.at \
  proc_helpers.at \
  forbidden_words.at \
  client.at

TESTSUITE_AT_IN =

if BUILD_BUGZILLA
TESTSUITE_AT_IN += bugzilla_plugin.at
endif

EXTRA_DIST += $(TESTSUITE_AT)
TESTSUITE = $(srcdir)/testsuite
MAINTAINERCLEANFILES = Makefile.in $(TESTSUITE)
check_DATA = atconfig atlocal $(TESTSUITE)
DISTCLEANFILES = atconfig
EXTRA_DIST += atlocal.in conf rules ureport valgrind.supp \
			  bugzilla_plugin.at.in

atconfig: $(top_builddir)/config.status
	(cd ${top_builddir} && ./config.status ${subdir}/atconfig)

if BUILD_PYTHON3
BUILD_PYTHON3=1
endif

check-local: $(check_DATA)
	export AUGEAS_LENS_LIB="$(AUGEAS_LENS_LIB_DIR):$(abs_top_srcdir)/data/augeas"; \
	$(SHELL) '$(TESTSUITE)' $(TESTSUITEFLAGS) BUILD_PYTHON3=$(BUILD_PYTHON3)

installcheck-local: $(check_DATA)
	$(SHELL) '$(TESTSUITE)' AUTOTEST_PATH='$(bindir)' $(TESTSUITEFLAGS)

clean-local:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' --clean

AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): $(TESTSUITE_AT) $(TESTSUITE_AT_IN) $(srcdir)/package.m4
	$(AUTOTEST) -I '$(srcdir)' -o $@.tmp $@.at
	mv $@.tmp $@

.PHONY: maintainer-check-valgrind
maintainer-check-valgrind: $(check_DATA)
	$(MAKE) check-local \
		PRE_AT_CHECK='$(abs_top_builddir)/libtool --mode=execute $(VALGRIND)' \
		VALGRIND_OPTS='--tool=memcheck --leak-check=full --show-reachable=yes --error-exitcode=1 --suppressions=$(abs_top_builddir)/tests/valgrind.supp -q'

.PHONY: maintainer-check
maintainer-check: maintainer-check-valgrind
