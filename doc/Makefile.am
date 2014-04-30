# silent rules for ASCIIDOC and XMLTO
ASCIIDOC_SILENT = $(ASCIIDOC_SILENT_$(V))
ASCIIDOC_SILENT_ = $(ASCIIDOC_SILENT_$(AM_DEFAULT_VERBOSITY))
ASCIIDOC_SILENT_0 = @echo ASCIIDOC $@;

XMLTO_SILENT = $(XMLTO_SILENT_$(V))
XMLTO_SILENT_ = $(XMLTO_SILENT_$(AM_DEFAULT_VERBOSITY))
XMLTO_SILENT_0 = @echo "  XMLTO " $@;

MAN1_TXT =
MAN1_TXT += report-cli.txt
MAN1_TXT += reporter-bugzilla.txt
MAN1_TXT += reporter-kerneloops.txt
MAN1_TXT += reporter-mailx.txt
MAN1_TXT += reporter-print.txt
MAN1_TXT += reporter-rhtsupport.txt
MAN1_TXT += reporter-upload.txt
MAN1_TXT += reporter-ureport.txt
MAN1_TXT += report.txt

MAN5_TXT =
MAN5_TXT += report_event.conf.txt
MAN5_TXT += ureport.conf.txt

# Manual pages are generated from .txt via Docbook
man1_MANS = ${MAN1_TXT:%.txt=%.1}
man5_MANS = ${MAN5_TXT:%.txt=%.5}

%.1 %.5: %.xml
	$(XMLTO_SILENT) xmlto man $< 2>&1 | sed '/Note/d'

%.xml: %.txt ../asciidoc.conf
	$(ASCIIDOC_SILENT) asciidoc --backend=docbook --doctype=manpage \
                                    --conf-file ../asciidoc.conf \
                                    -alibreport_version=$(PACKAGE_VERSION) -o $@ $<

EXTRA_DIST = $(MAN1_TXT) $(MAN5_TXT)
CLEANFILES = $(man1_MANS)
