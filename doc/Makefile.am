# silent rules for ASCIIDOC and XMLTO
ASCIIDOC_SILENT = $(ASCIIDOC_SILENT_$(V))
ASCIIDOC_SILENT_ = $(ASCIIDOC_SILENT_$(AM_DEFAULT_VERBOSITY))
ASCIIDOC_SILENT_0 = @echo ASCIIDOC $@;

XMLTO_SILENT = $(XMLTO_SILENT_$(V))
XMLTO_SILENT_ = $(XMLTO_SILENT_$(AM_DEFAULT_VERBOSITY))
XMLTO_SILENT_0 = @echo "  XMLTO " $@;

MAN1_TXT =
MAN1_TXT += report-cli.txt
MAN1_TXT += report-newt.txt
MAN1_TXT += report-gtk.txt

if BUILD_BUGZILLA
MAN1_TXT += reporter-bugzilla.txt
MAN1_TXT += report.txt
endif

MAN1_TXT += reporter-kerneloops.txt
MAN1_TXT += reporter-mailx.txt
MAN1_TXT += reporter-print.txt
MAN1_TXT += reporter-rhtsupport.txt
MAN1_TXT += reporter-upload.txt
MAN1_TXT += reporter-ureport.txt
MAN1_TXT += reporter-mantisbt.txt
MAN1_TXT += reporter-systemd-journal.txt

MAN5_TXT =
MAN5_TXT += anaconda_event.conf.txt
MAN5_TXT += bugzilla_anaconda_event.conf.txt
MAN5_TXT += bugzilla.conf.txt
MAN5_TXT += bugzilla_event.conf.txt
MAN5_TXT += bugzilla_format_anaconda.conf.txt
MAN5_TXT += bugzilla_format.conf.txt
MAN5_TXT += bugzilla_formatdup_anaconda.conf.txt
MAN5_TXT += bugzilla_formatdup.conf.txt
MAN5_TXT += bugzilla_format_kernel.conf.txt
MAN5_TXT += bugzilla_format_analyzer_libreport.conf.txt
MAN5_TXT += bugzilla_formatdup_analyzer_libreport.conf.txt
MAN5_TXT += mantisbt.conf.txt
MAN5_TXT += mantisbt_format.conf.txt
MAN5_TXT += mantisbt_formatdup.conf.txt
MAN5_TXT += mantisbt_format_analyzer_libreport.conf.txt
MAN5_TXT += mantisbt_formatdup_analyzer_libreport.conf.txt
MAN5_TXT += ignored_words.conf.txt
MAN5_TXT += forbidden_words.conf.txt
MAN5_TXT += libreport.conf.txt
MAN5_TXT += mailx.conf.txt
MAN5_TXT += mailx_event.conf.txt
MAN5_TXT += print_event.conf.txt
MAN5_TXT += report_Bugzilla.conf.txt
MAN5_TXT += report_event.conf.txt
MAN5_TXT += report_fedora.conf.txt
MAN5_TXT += report_centos.conf.txt
MAN5_TXT += report_Logger.conf.txt
MAN5_TXT += report_rhel.conf.txt
MAN5_TXT += report_rhel_bugzilla.conf.txt
MAN5_TXT += report_uReport.conf.txt
MAN5_TXT += report_logger.conf.txt
MAN5_TXT += report_mailx.conf.txt
MAN5_TXT += report_uploader.conf.txt
MAN5_TXT += report_Uploader.conf.txt
MAN5_TXT += report_CentOSBugTracker.conf.txt
MAN5_TXT += rhtsupport.conf.txt
MAN5_TXT += rhtsupport_event.conf.txt
MAN5_TXT += uploader_event.conf.txt
MAN5_TXT += ureport.conf.txt
MAN5_TXT += upload.conf.txt

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

EXTRA_DIST = $(MAN1_TXT) $(MAN5_TXT) $(MAN5_PREFORMATTED)
CLEANFILES = $(man1_MANS)
