ACLOCAL_AMFLAGS = -I m4
SUBDIRS = po src tests doc

EXTRA_DIST = \
    libreport.pc.in \
    libreport-version \
    asciidoc.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libreport.pc
pkgconfig_DATA += libreport-web.pc

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

release-fix:
	OLD_VER=$$(git describe --tags --match "[0-9]*" --abbrev=0 HEAD 2>/dev/null); \
	Mm_VER=$$(echo $$OLD_VER | cut -d. -f 1,2); \
	F_VER=$$(echo $$OLD_VER | cut -d. -f 3); \
	MmF_VER="$$Mm_VER.$$((F_VER+1))"; \
	echo "* $$(date +'%a %b %d %Y') $$(git config --get user.name) <$$(git config --get user.email)> $$MmF_VER-1" > /tmp/changelog.tmp; \
	git log --oneline $$OLD_VER..HEAD | awk '{$$1=""; print "-" $$0} END {print ""}' >> /tmp/changelog.tmp; \
	sed "$$(grep -n changelog libreport.spec.in | cut -f1 -d:)"'r /tmp/changelog.tmp' -i libreport.spec.in; \
	git add libreport.spec.in; \
	git commit -m "New version $$MmF_VER"; \
	git tag "$$MmF_VER"; \
	./gen-version
	autoconf --force
	$(MAKE) dist
