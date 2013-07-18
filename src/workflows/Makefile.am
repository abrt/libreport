workflowsdir = $(WORKFLOWS_DIR)

dist_workflows_DATA = \
    workflow_FedoraCCpp.xml \
    workflow_FedoraKerneloops.xml \
    workflow_FedoraPython.xml \
    workflow_FedoraVmcore.xml \
    workflow_FedoraXorg.xml \
    workflow_Upload.xml \
    workflow_RHELCCpp.xml \
    workflow_RHELKerneloops.xml \
    workflow_RHELPython.xml \
    workflow_RHELvmcore.xml \
    workflow_RHELxorg.xml

if BUILD_BUGZILLA
dist_workflows_DATA += \
    workflow_AnacondaFedora.xml \
    workflow_AnacondaRHEL.xml \
    workflow_AnacondaUpload.xml
endif

workflowsdefdir = $(WORKFLOWS_DEFINITION_DIR)

dist_workflowsdef_DATA =\
    report_fedora.conf \
    report_rhel.conf

if BUILD_BUGZILLA
dist_workflowsdef_DATA += \
    anaconda_event.conf
endif

@INTLTOOL_XML_RULE@

EXTRA_DIST = \
    workflow_FedoraCCpp.xml.in \
    workflow_FedoraKerneloops.xml.in \
    workflow_FedoraPython.xml.in \
    workflow_FedoraVmcore.xml.in \
    workflow_FedoraXorg.xml.in \
    workflow_Upload.xml.in \
    workflow_RHELCCpp.xml.in \
    workflow_RHELKerneloops.xml.in \
    workflow_RHELPython.xml.in \
    workflow_RHELvmcore.xml.in \
    workflow_RHELxorg.xml.in

if BUILD_BUGZILLA
EXTRA_DIST += \
    workflow_AnacondaFedora.xml.in \
    workflow_AnacondaRHEL.xml.in \
    workflow_AnacondaUpload.xml.in
endif
