EXTRA_DIST =

MAN1_BUGZILLA = \
	reporter-bugzilla.txt \
	report.txt
MAN5_BUGZILLA = \
	bugzilla_anaconda_event.conf.txt \
	bugzilla.conf.txt \
	bugzilla_event.conf.txt \
	bugzilla_format_anaconda.conf.txt \
	bugzilla_format.conf.txt \
	bugzilla_formatdup_anaconda.conf.txt \
	bugzilla_formatdup.conf.txt \
	bugzilla_format_kernel.conf.txt \
	bugzilla_format_analyzer_libreport.conf.txt \
	bugzilla_formatdup_analyzer_libreport.conf.txt \
	report_Bugzilla.conf.txt

# silent rules for ASCIIDOC and XMLTO
ASCIIDOC_SILENT = $(ASCIIDOC_SILENT_$(V))
ASCIIDOC_SILENT_ = $(ASCIIDOC_SILENT_$(AM_DEFAULT_VERBOSITY))
ASCIIDOC_SILENT_0 = @echo ASCIIDOC $@;

XMLTO_SILENT = $(XMLTO_SILENT_$(V))
XMLTO_SILENT_ = $(XMLTO_SILENT_$(AM_DEFAULT_VERBOSITY))
XMLTO_SILENT_0 = @echo "  XMLTO " $@;

MAN1_TXT = \
	report-cli.txt \
	report-newt.txt \
	report-gtk.txt \
	reporter-kerneloops.txt \
	reporter-mailx.txt \
	reporter-print.txt \
	reporter-rhtsupport.txt \
	reporter-upload.txt \
	reporter-ureport.txt \
	reporter-mantisbt.txt \
	reporter-systemd-journal.txt

MAN5_TXT = \
	anaconda_event.conf.txt \
	mantisbt.conf.txt \
	mantisbt_format.conf.txt \
	mantisbt_formatdup.conf.txt \
	mantisbt_format_analyzer_libreport.conf.txt \
	mantisbt_formatdup_analyzer_libreport.conf.txt \
	ignored_words.conf.txt \
	forbidden_words.conf.txt \
	libreport.conf.txt \
	mailx.conf.txt \
	mailx_event.conf.txt \
	print_event.conf.txt \
	report_event.conf.txt \
	report_fedora.conf.txt \
	report_centos.conf.txt \
	report_Logger.conf.txt \
	report_rhel.conf.txt \
	report_rhel_bugzilla.conf.txt \
	report_uReport.conf.txt \
	report_logger.conf.txt \
	report_mailx.conf.txt \
	report_uploader.conf.txt \
	report_Uploader.conf.txt \
	report_CentOSBugTracker.conf.txt \
	rhtsupport.conf.txt \
	rhtsupport_event.conf.txt \
	uploader_event.conf.txt \
	ureport.conf.txt \
	upload.conf.txt

if BUILD_BUGZILLA
MAN1_TXT += $(MAN1_BUGZILLA)
MAN5_TXT += $(MAN5_BUGZILLA)
else
EXTRA_DIST += $(MAN1_BUGZILLA) $(MAN5_BUGZILLA)
endif

MAN5_PREFORMATTED =
MAN5_PREFORMATTED += centos_report_event.conf.5

# Manual pages are generated from .txt via Docbook
man1_MANS = ${MAN1_TXT:%.txt=%.1}
man5_MANS = ${MAN5_TXT:%.txt=%.5} ${MAN5_PREFORMATTED}

SUFFIXES = .1 .5

.xml.1:
	$(XMLTO_SILENT) xmlto man $< 2>&1 | sed '/Note/d'

.xml.5:
	$(XMLTO_SILENT) xmlto man $< 2>&1 | sed '/Note/d'

.txt.xml: ../asciidoc.conf
	$(ASCIIDOC_SILENT) asciidoc --backend=docbook --doctype=manpage \
                                    --conf-file $(top_srcdir)/asciidoc.conf \
                                    -alibreport_version=$(PACKAGE_VERSION) -o $@ $<

EXTRA_DIST += $(MAN1_TXT) $(MAN5_TXT) $(MAN5_PREFORMATTED)
CLEANFILES = $(man1_MANS)
