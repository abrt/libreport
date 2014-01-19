INTERFACES_DIST_FILES=$(wildcard *.xml.in)

%.xml: %.xml.in
	cp $< $@

dbusplugininterfacesdir = ${dbusinterfacedir}
dist_dbusplugininterfaces_DATA = \
	com.redhat.problems.configuration.bugzilla.xml \
	com.redhat.problems.configuration.mailx.xml \
	com.redhat.problems.configuration.rhtsupport.xml \
	com.redhat.problems.configuration.ureport.xml

EXTRA_DIST = ${INTERFACES_DIST_FILES}
