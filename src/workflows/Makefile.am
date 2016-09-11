workflowsdir = $(WORKFLOWS_DIR)

dist_workflows_DATA = \
    workflow_FedoraCCpp.xml \
    workflow_FedoraKerneloops.xml \
    workflow_FedoraPython.xml \
    workflow_FedoraPython3.xml \
    workflow_FedoraVmcore.xml \
    workflow_FedoraXorg.xml \
    workflow_FedoraLibreport.xml \
    workflow_FedoraJava.xml \
    workflow_FedoraJavaScript.xml \
    workflow_RHELCCpp.xml \
    workflow_RHELKerneloops.xml \
    workflow_RHELPython.xml \
    workflow_RHELvmcore.xml \
    workflow_RHELxorg.xml \
    workflow_RHELLibreport.xml \
    workflow_RHELJava.xml \
    workflow_RHELJavaScript.xml \
    workflow_uReport.xml \
    workflow_Mailx.xml \
    workflow_MailxCCpp.xml \
    workflow_Upload.xml \
    workflow_UploadCCpp.xml \
    workflow_Logger.xml \
    workflow_LoggerCCpp.xml

if BUILD_MANTISBT
dist_workflows_DATA += \
    workflow_CentOSCCpp.xml \
    workflow_CentOSKerneloops.xml \
    workflow_CentOSPython.xml \
    workflow_CentOSPython3.xml \
    workflow_CentOSVmcore.xml \
    workflow_CentOSXorg.xml \
    workflow_CentOSLibreport.xml \
    workflow_CentOSJava.xml \
    workflow_CentOSJavaScript.xml
endif

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
    workflow_RHELBugzillaJava.xml \
    workflow_RHELBugzillaJavaScript.xml
endif

workflowsdefdir = $(WORKFLOWS_DEFINITION_DIR)

dist_workflowsdef_DATA =\
    report_fedora.conf \
    report_rhel.conf \
    report_uReport.conf \
    report_mailx.conf \
    report_logger.conf \
    report_uploader.conf \
    report_centos.conf

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
    workflow_FedoraPython3.xml.in \
    workflow_FedoraVmcore.xml.in \
    workflow_FedoraXorg.xml.in \
    workflow_FedoraLibreport.xml.in \
    workflow_FedoraJava.xml.in \
    workflow_FedoraJavaScript.xml.in \
    workflow_RHELCCpp.xml.in \
    workflow_RHELKerneloops.xml.in \
    workflow_RHELPython.xml.in \
    workflow_RHELvmcore.xml.in \
    workflow_RHELxorg.xml.in \
    workflow_RHELLibreport.xml.in \
    workflow_RHELJava.xml.in \
    workflow_RHELJavaScript.xml.in \
    workflow_uReport.xml.in \
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
    workflow_RHELBugzillaJava.xml.in \
    workflow_RHELBugzillaJavaScript.xml.in
endif

if BUILD_MANTISBT
EXTRA_DIST += \
    workflow_CentOSCCpp.xml.in \
    workflow_CentOSKerneloops.xml.in \
    workflow_CentOSPython.xml.in \
    workflow_CentOSPython3.xml.in \
    workflow_CentOSVmcore.xml.in \
    workflow_CentOSXorg.xml.in \
    workflow_CentOSLibreport.xml.in \
    workflow_CentOSJava.xml.in \
    workflow_CentOSJavaScript.xml.in
endif
