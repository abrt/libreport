workflowsdir = $(WORKFLOWS_DIR)

dist_workflows_DATA = \
    workflow_Fedora.xml \
    workflow_Upload.xml \
    workflow_RHELCCpp.xml \
    workflow_RHELKerneloops.xml \
    workflow_RHELPython.xml \
    workflow_RHELvmcore.xml \
    workflow_RHELxorg.xml

if BUILD_BUGZILLA
dist_workflows_DATA += \
    workflow_AnacondaFedora.xml \
    workflow_AnacondaUpload.xml
endif

workflowsconfdir = $(WORKFLOWS_CONF_DIR)

dist_workflowsconf_DATA =\
    report_fedora.conf \
    report_rhel.conf

if BUILD_BUGZILLA
dist_workflowsconf_DATA += \
    anaconda_event.conf
endif

@INTLTOOL_XML_RULE@

EXTRA_DIST = \
    workflow_Fedora.xml.in \
    workflow_Upload.xml.in \
    workflow_RHELCCpp.xml.in \
    workflow_RHELKerneloops.xml.in \
    workflow_RHELPython.xml.in \
    workflow_RHELvmcore.xml.in \
    workflow_RHELxorg.xml.in

if BUILD_BUGZILLA
EXTRA_DIST += \
    workflow_AnacondaFedora.xml.in \
    workflow_AnacondaUpload.xml.in
endif
