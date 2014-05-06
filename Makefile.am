ACLOCAL_AMFLAGS = -I m4
SUBDIRS = po src tests doc

EXTRA_DIST = \
    libreport.pc.in \
    libreport-version \
    asciidoc.conf \
    augeas/test_libreport.aug

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libreport.pc

augeasdir = $(AUGEAS_LENS_LIB_DIR)
dist_augeas_DATA = augeas/libreport.aug

check-local:
	$(AUGPARSE) -I $(top_builddir)/augeas $(top_builddir)/augeas/test_libreport.aug

RPM_DIRS = --define "_sourcedir `pwd`" \
	   --define "_rpmdir `pwd`" \
	   --define "_specdir `pwd`" \
	   --define "_builddir `pwd`" \
	   --define "_srcrpmdir `pwd`"

rpm:
	./gen-version
	autoconf --force
	$(MAKE) dist
	rpmbuild $(RPM_DIRS) -ba libreport.spec

srpm:
	./gen-version
	autoconf --force
	$(MAKE) dist
	rpmbuild $(RPM_DIRS) -bs libreport.spec

# value for overriding from command line
# e.g: make scratch-build DIST=f15
DIST=rawhide
scratch-build: srpm
	koji build --scratch dist-$(DIST) `make srpm | grep Wrote | cut -d' ' -f2`
