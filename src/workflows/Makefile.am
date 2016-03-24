workflowsdir = $(WORKFLOWS_DIR)

dist_workflows_DATA = \
    workflow_FedoraCCpp.xml \
    workflow_FedoraKerneloops.xml \
    workflow_FedoraPython.xml \
    workflow_FedoraVmcore.xml \
    workflow_FedoraXorg.xml \
    workflow_FedoraLibreport.xml \
    workflow_FedoraJava.xml \
    workflow_RHELCCpp.xml \
    workflow_RHELKerneloops.xml \
    workflow_RHELPython.xml \
    workflow_RHELvmcore.xml \
    workflow_RHELxorg.xml \
    workflow_RHELLibreport.xml \
    workflow_RHELJava.xml \
    workflow_uReport.xml \
    workflow_Mailx.xml \
    workflow_MailxCCpp.xml \
    workflow_Upload.xml \
    workflow_UploadCCpp.xml \
    workflow_Logger.xml \
    workflow_LoggerCCpp.xml

if BUILD_BUGZILLA
dist_workflows_DATA += \
    workflow_AnacondaFedora.xml \
    workflow_AnacondaRHEL.xml \
    workflow_AnacondaRHELBugzilla.xml \
    workflow_AnacondaUpload.xml \
    workflow_RHELBugzillaCCpp.xml \
    workflow_RHELBugzillaKerneloops.xml \
    workflow_RHELBugzillaPython.xml \
    workflow_RHELBugzillaVmcore.xml \
    workflow_RHELBugzillaXorg.xml \
    workflow_RHELBugzillaLibreport.xml \
    workflow_RHELBugzillaJava.xml
endif

workflowsdefdir = $(WORKFLOWS_DEFINITION_DIR)

dist_workflowsdef_DATA =\
    report_fedora.conf \
    report_rhel.conf \
    report_uReport.conf \
    report_mailx.conf \
    report_logger.conf \
    report_uploader.conf

if BUILD_BUGZILLA
dist_workflowsdef_DATA += \
    anaconda_event.conf \
    report_rhel_bugzilla.conf
endif

@INTLTOOL_XML_RULE@

EXTRA_DIST = \
    workflow_FedoraCCpp.xml.in \
    workflow_FedoraKerneloops.xml.in \
    workflow_FedoraPython.xml.in \
    workflow_FedoraVmcore.xml.in \
    workflow_FedoraXorg.xml.in \
    workflow_FedoraLibreport.xml.in \
    workflow_FedoraJava.xml.in \
    workflow_uReport.xml.in \
    workflow_RHELCCpp.xml.in \
    workflow_RHELKerneloops.xml.in \
    workflow_RHELPython.xml.in \
    workflow_RHELvmcore.xml.in \
    workflow_RHELxorg.xml.in \
    workflow_RHELLibreport.xml.in \
    workflow_RHELJava.xml.in \
    workflow_Mailx.xml.in \
    workflow_MailxCCpp.xml.in \
    workflow_Upload.xml.in \
    workflow_UploadCCpp.xml.in \
    workflow_Logger.xml.in \
    workflow_LoggerCCpp.xml.in


if BUILD_BUGZILLA
EXTRA_DIST += \
    workflow_AnacondaFedora.xml.in \
    workflow_AnacondaRHEL.xml.in \
    workflow_AnacondaRHELBugzilla.xml.in \
    workflow_AnacondaUpload.xml.in \
    workflow_RHELBugzillaCCpp.xml.in \
    workflow_RHELBugzillaKerneloops.xml.in \
    workflow_RHELBugzillaPython.xml.in \
    workflow_RHELBugzillaVmcore.xml.in \
    workflow_RHELBugzillaXorg.xml.in \
    workflow_RHELBugzillaLibreport.xml.in \
    workflow_RHELBugzillaJava.xml.in
endif
