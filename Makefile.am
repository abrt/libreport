ACLOCAL_AMFLAGS = -I m4
SUBDIRS = data po src tests doc apidoc

EXTRA_DIST = \
    libreport.pc.in \
    libreport-version \
    asciidoc.conf \
    README.md

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libreport.pc
pkgconfig_DATA += libreport-web.pc

rpm:
	tito build --rpm

srpm:
	tito build --srpm

# value for overriding from command line
# e.g: make scratch-build DIST=f15
DIST=rawhide
scratch-build: srpm
	koji build --scratch $(DIST) `make srpm | grep Wrote | cut -d' ' -f2`

UPLOAD_URL ?= localhost

upload: dist
	scp $(distdir).tar.gz $$(test -n "$$UPLOAD_LOGIN" && echo "$$UPLOAD_LOGIN@")$(UPLOAD_URL)

.PHONY: release
release:
	tito tag
