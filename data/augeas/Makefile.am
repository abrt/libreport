augeasdir = $(datadir)/augeas/lenses
dist_augeas_DATA = libreport.aug

check-local:
	$(AUGPARSE) -I "$(abs_builddir)" "$(abs_builddir)/test_libreport.aug"

EXTRA_DIST = test_libreport.aug
