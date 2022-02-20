%if 0%{?suse_version}
  %bcond_with bugzilla

  %define dbus_devel dbus-1-devel
  %define libjson_devel libjson-devel
%else
  %bcond_without bugzilla

  %define dbus_devel dbus-devel
  %define libjson_devel json-c-devel
%endif

%define glib_ver 2.43.4

Summary: Generic library for reporting various problems
Name: libreport
Version: 2.17.0
Release: 1%{?dist}
License: GPLv2+
URL: https://abrt.readthedocs.org/
Source: https://github.com/abrt/%{name}/archive/%{version}/%{name}-%{version}.tar.gz
BuildRequires: %{dbus_devel}
BuildRequires: gtk3-devel
BuildRequires: curl-devel
BuildRequires: desktop-file-utils
BuildRequires: python3-devel
BuildRequires: gettext
BuildRequires: libxml2-devel
BuildRequires: intltool
BuildRequires: libtool
BuildRequires: make
BuildRequires: texinfo
BuildRequires: asciidoc
BuildRequires: xmlto
BuildRequires: newt-devel
BuildRequires: satyr-devel >= 0.38
BuildRequires: glib2-devel >= %{glib_ver}
BuildRequires: git-core

%if 0%{?fedora} >= 24 || 0%{?rhel} > 7
# A test case uses zh_CN locale to verify XML event translations
BuildRequires: glibc-all-langpacks
%endif

%if %{with bugzilla}
BuildRequires: xmlrpc-c-devel
%endif
BuildRequires: doxygen
BuildRequires: systemd-devel
BuildRequires: augeas-devel
BuildRequires: augeas
BuildRequires: libarchive-devel
Requires: libreport-filesystem = %{version}-%{release}
Requires: satyr%{?_isa} >= 0.38
Requires: glib2%{?_isa} >= %{glib_ver}
Requires: libarchive%{?_isa}

# Required for the temporary modularity hack, see below
%if 0%{?_module_build}
BuildRequires: sed
%endif

Obsoletes: %{name}-compat < 2.13.2
Obsoletes: %{name}-plugin-rhtsupport < 2.13.2
Obsoletes: %{name}-rhel < 2.13.2

%description
Libraries providing API for reporting different problems in applications
to different bug targets like Bugzilla, ftp, trac, etc...

%package filesystem
Summary: Filesystem layout for libreport
BuildArch: noarch

%description filesystem
Filesystem layout for libreport

%package devel
Summary: Development libraries and headers for libreport
Requires: libreport = %{version}-%{release}

%description devel
Development libraries and headers for libreport

%package web
Summary: Library providing network API for libreport
Requires: libreport = %{version}-%{release}

%description web
Library providing network API for libreport

%package web-devel
Summary: Development headers for libreport-web
Requires: libreport-web = %{version}-%{release}

%description web-devel
Development headers for libreport-web

%package -n python3-libreport
Summary: Python 3 bindings for report-libs
%if 0%{?_module_build}
# This is required for F26 Boltron (the modular release)
# Different parts of libreport are shipped with different
# modules with different dist tags; we need to weaken the
# strict NVR dependency to make it work.  Temporary and
# limited to F26 Boltron.
%global distfreerelease %(echo %{release}|sed 's/%{?dist}$//'||echo 0)
Requires: libreport >= %{version}-%{distfreerelease}
%else
Requires: libreport = %{version}-%{release}
%endif
Requires: python3-dnf
%{?python_provide:%python_provide python3-libreport}

%description -n python3-libreport
Python 3 bindings for report-libs.

%package cli
Summary: %{name}'s command line interface
Requires: %{name} = %{version}-%{release}

%description cli
This package contains simple command line tool for working
with problem dump reports

%package newt
Summary: %{name}'s newt interface
Requires: %{name} = %{version}-%{release}
Provides: report-newt = 0:0.23-1
Obsoletes: report-newt < 0:0.23-1

%description newt
This package contains a simple newt application for reporting
bugs

%package gtk
Summary: GTK front-end for libreport
Requires: libreport = %{version}-%{release}
Requires: libreport-plugin-reportuploader = %{version}-%{release}
Provides: report-gtk = 0:0.23-1
Obsoletes: report-gtk < 0:0.23-1

%description gtk
Applications for reporting bugs using libreport backend

%package gtk-devel
Summary: Development libraries and headers for libreport
Requires: libreport-gtk = %{version}-%{release}

%description gtk-devel
Development libraries and headers for libreport-gtk

%package plugin-kerneloops
Summary: %{name}'s kerneloops reporter plugin
Requires: curl
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}

%description plugin-kerneloops
This package contains plugin which sends kernel crash information to specified
server, usually to kerneloops.org.

%package plugin-logger
Summary: %{name}'s logger reporter plugin
Requires: %{name} = %{version}-%{release}

%description plugin-logger
The simple reporter plugin which writes a report to a specified file.

%package plugin-systemd-journal
Summary: %{name}'s systemd journal reporter plugin
Requires: %{name} = %{version}-%{release}

%description plugin-systemd-journal
The simple reporter plugin which writes a report to the systemd journal.

%package plugin-mailx
Summary: %{name}'s mailx reporter plugin
Requires: %{name} = %{version}-%{release}
Requires: /usr/bin/mailx

%description plugin-mailx
The simple reporter plugin which sends a report via mailx to a specified
email address.

%if %{with bugzilla}
%package plugin-bugzilla
Summary: %{name}'s bugzilla plugin
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}

%description plugin-bugzilla
Plugin to report bugs into the bugzilla.
%endif

%package plugin-mantisbt
Summary: %{name}'s mantisbt plugin
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}

%description plugin-mantisbt
Plugin to report bugs into the mantisbt.

%package centos
Summary: %{name}'s CentOS Bug Tracker workflow
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}
Requires: libreport-plugin-mantisbt = %{version}-%{release}

%description centos
Workflows to report issues into the CentOS Bug Tracker.

%package plugin-ureport
Summary: %{name}'s micro report plugin
BuildRequires: %{libjson_devel}
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}

%description plugin-ureport
Uploads micro-report to abrt server

%package plugin-reportuploader
Summary: %{name}'s reportuploader plugin
Requires: %{name} = %{version}-%{release}
Requires: libreport-web = %{version}-%{release}

%description plugin-reportuploader
Plugin to report bugs into anonymous FTP site associated with ticketing system.

%if 0%{?fedora} || 0%{?eln}
%package fedora
Summary: Default configuration for reporting bugs via Fedora infrastructure
Requires: %{name} = %{version}-%{release}

%description fedora
Default configuration for reporting bugs via Fedora infrastructure
used to easily configure the reporting process for Fedora systems. Just
install this package and you're done.
%endif

%if 0%{?rhel} && ! 0%{?eln}
%package rhel-bugzilla
Summary: Default configuration for reporting bugs to Red Hat Bugzilla
Requires: %{name} = %{version}-%{release}
Requires: libreport-plugin-bugzilla = %{version}-%{release}
Requires: libreport-plugin-ureport = %{version}-%{release}

%description rhel-bugzilla
Default configuration for reporting bugs to Red Hat Bugzilla used to easily
configure the reporting process for Red Hat systems. Just install this package
and you're done.

%package rhel-anaconda-bugzilla
Summary: Default configuration for reporting anaconda bugs to Red Hat Bugzilla
Requires: %{name} = %{version}-%{release}
Requires: libreport-plugin-bugzilla = %{version}-%{release}

%description rhel-anaconda-bugzilla
Default configuration for reporting Anaconda problems to Red Hat Bugzilla used
to easily configure the reporting process for Red Hat systems. Just install this
package and you're done.
%endif

%if %{with bugzilla}
%package anaconda
Summary: Default configuration for reporting anaconda bugs
Requires: %{name} = %{version}-%{release}
Requires: libreport-plugin-reportuploader = %{version}-%{release}
%if ! 0%{?rhel} || 0%{?eln}
Requires: libreport-plugin-bugzilla = %{version}-%{release}
%endif

%description anaconda
Default configuration for reporting Anaconda problems or uploading the gathered
data over ftp/scp...
%endif

%prep
%autosetup

%build
./autogen.sh

%configure \
%if %{without bugzilla}
        --without-bugzilla \
%endif
        --enable-doxygen-docs \
        --disable-silent-rules

%make_build

%install
%make_install \
%if %{with python3}
             PYTHON=%{__python3} \
%endif
             mandir=%{_mandir}

%find_lang %{name}

# Remove byte-compiled python files generated by automake.
# automake uses system's python for all *.py files, even
# for those which needs to be byte-compiled with different
# version (python2/python3).
# rpm can do this work and use the appropriate python version.
find %{buildroot} -name "*.py[co]" -delete

# remove all .la and .a files
find %{buildroot} -name '*.la' -or -name '*.a' | xargs rm -f
mkdir -p %{buildroot}/%{_initrddir}
mkdir -p %{buildroot}/%{_sysconfdir}/%{name}/events.d/
mkdir -p %{buildroot}/%{_sysconfdir}/%{name}/events/
mkdir -p %{buildroot}/%{_sysconfdir}/%{name}/workflows.d/
mkdir -p %{buildroot}/%{_datadir}/%{name}/events/
mkdir -p %{buildroot}/%{_datadir}/%{name}/workflows/

# After everything is installed, remove info dir
rm -f %{buildroot}/%{_infodir}/dir

# Remove unwanted Fedora specific workflow configuration files
%if ! 0%{?fedora} && ! 0%{?eln}
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraCCpp.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraKerneloops.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraPython.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraPython3.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraVmcore.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraXorg.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraLibreport.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraJava.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_FedoraJavaScript.xml
rm -f %{buildroot}/%{_sysconfdir}/libreport/workflows.d/report_fedora.conf
rm -f %{buildroot}%{_mandir}/man5/report_fedora.conf.5
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_AnacondaFedora.xml
%endif

# Remove unwanted RHEL specific workflow configuration files
%if ! 0%{?rhel} || 0%{?eln}
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_uReport.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_AnacondaRHELBugzilla.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaCCpp.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaKerneloops.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaPython.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaVmcore.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaXorg.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaLibreport.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaJava.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELBugzillaJavaScript.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataCCpp.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataKerneloops.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataPython.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDatavmcore.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataxorg.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataLibreport.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataJava.xml
rm -f %{buildroot}/%{_datadir}/libreport/workflows/workflow_RHELAddDataJavaScript.xml
rm -f %{buildroot}/%{_sysconfdir}/libreport/workflows.d/report_uReport.conf
rm -f %{buildroot}/%{_sysconfdir}/libreport/workflows.d/report_rhel_bugzilla.conf
rm -f %{buildroot}%{_mandir}/man5/report_uReport.conf.5
rm -f %{buildroot}%{_mandir}/man5/report_rhel_bugzilla.conf.5
%endif

%check
make check|| {
    # find and print the logs of failed test
    # do not cat tests/testsuite.log because it contains a lot of bloat
    find tests/testsuite.dir -name "testsuite.log" -print -exec cat '{}' \;
    exit 1
}

%ldconfig_scriptlets
%ldconfig_scriptlets web
%if 0%{?rhel} && 0%{?rhel} <= 7
%post gtk
%{?ldconfig}
# update icon cache
touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :

%postun gtk
%{?ldconfig}
if [ $1 -eq 0 ] ; then
    touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%posttrans gtk
gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :

%endif

%files -f %{name}.lang
%doc README.md
%license COPYING
%config(noreplace) %{_sysconfdir}/%{name}/libreport.conf
%config(noreplace) %{_sysconfdir}/%{name}/report_event.conf
%config(noreplace) %{_sysconfdir}/%{name}/forbidden_words.conf
%config(noreplace) %{_sysconfdir}/%{name}/ignored_words.conf
%config(noreplace) %{_sysconfdir}/%{name}/ignored_elements.conf
%{_datadir}/%{name}/conf.d/libreport.conf
%{_libdir}/libreport.so.*
%{_mandir}/man5/libreport.conf.5*
%{_mandir}/man5/report_event.conf.5*
%{_mandir}/man5/forbidden_words.conf.5*
%{_mandir}/man5/ignored_words.conf.5*
%{_mandir}/man5/ignored_elements.conf.5*
# filesystem package owns /usr/share/augeas/lenses directory
%{_datadir}/augeas/lenses/libreport.aug

%files filesystem
%dir %{_sysconfdir}/%{name}/
%dir %{_sysconfdir}/%{name}/events.d/
%dir %{_sysconfdir}/%{name}/events/
%dir %{_sysconfdir}/%{name}/workflows.d/
%dir %{_datadir}/%{name}/
%dir %{_datadir}/%{name}/conf.d/
%dir %{_datadir}/%{name}/conf.d/plugins/
%dir %{_datadir}/%{name}/events/
%dir %{_datadir}/%{name}/workflows/
%dir %{_sysconfdir}/%{name}/plugins/

%files devel
# Public api headers:
%doc apidoc/html/*.{html,png,css,js}
%{_includedir}/libreport/libreport_types.h
%{_includedir}/libreport/client.h
%{_includedir}/libreport/dump_dir.h
%{_includedir}/libreport/event_config.h
%{_includedir}/libreport/problem_data.h
%{_includedir}/libreport/problem_report.h
%{_includedir}/libreport/report.h
%{_includedir}/libreport/report_result.h
%{_includedir}/libreport/run_event.h
%{_includedir}/libreport/file_obj.h
%{_includedir}/libreport/config_item_info.h
%{_includedir}/libreport/workflow.h
%{_includedir}/libreport/problem_details_widget.h
%{_includedir}/libreport/problem_details_dialog.h
%{_includedir}/libreport/problem_utils.h
%{_includedir}/libreport/ureport.h
%{_includedir}/libreport/reporters.h
%{_includedir}/libreport/global_configuration.h
# Private api headers:
%{_includedir}/libreport/internal_libreport.h
%{_includedir}/libreport/xml_parser.h
%{_includedir}/libreport/helpers
%{_libdir}/libreport.so
%{_libdir}/pkgconfig/libreport.pc
%dir %{_includedir}/libreport

%files web
%{_libdir}/libreport-web.so.*

%files web-devel
%{_libdir}/libreport-web.so
%{_includedir}/libreport/libreport_curl.h
%{_libdir}/pkgconfig/libreport-web.pc

%files -n python3-libreport
%{python3_sitearch}/report/
%{python3_sitearch}/reportclient/

%files cli
%{_bindir}/report-cli
%{_mandir}/man1/report-cli.1.gz

%files newt
%{_bindir}/report-newt
%{_mandir}/man1/report-newt.1.gz

%files gtk
%{_bindir}/report-gtk
%{_libdir}/libreport-gtk.so.*
%{_mandir}/man1/report-gtk.1.gz

%files gtk-devel
%{_libdir}/libreport-gtk.so
%{_includedir}/libreport/internal_libreport_gtk.h
%{_libdir}/pkgconfig/libreport-gtk.pc

%files plugin-kerneloops
%{_datadir}/%{name}/events/report_Kerneloops.xml
%{_mandir}/man*/reporter-kerneloops.*
%{_bindir}/reporter-kerneloops

%files plugin-logger
%config(noreplace) %{_sysconfdir}/libreport/events/report_Logger.conf
%{_mandir}/man5/report_Logger.conf.5.*
%{_datadir}/%{name}/events/report_Logger.xml
%{_datadir}/%{name}/workflows/workflow_Logger.xml
%{_datadir}/%{name}/workflows/workflow_LoggerCCpp.xml
%config(noreplace) %{_sysconfdir}/libreport/events.d/print_event.conf
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_logger.conf
%{_mandir}/man5/print_event.conf.5.*
%{_mandir}/man5/report_logger.conf.5.*
%{_bindir}/reporter-print
%{_mandir}/man*/reporter-print.*

%files plugin-systemd-journal
%{_bindir}/reporter-systemd-journal
%{_mandir}/man*/reporter-systemd-journal.*

%files plugin-mailx
%config(noreplace) %{_sysconfdir}/libreport/plugins/mailx.conf
%{_datadir}/%{name}/conf.d/plugins/mailx.conf
%{_datadir}/%{name}/events/report_Mailx.xml
%{_datadir}/%{name}/workflows/workflow_Mailx.xml
%{_datadir}/%{name}/workflows/workflow_MailxCCpp.xml
%config(noreplace) %{_sysconfdir}/libreport/events.d/mailx_event.conf
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_mailx.conf
%{_mandir}/man5/mailx.conf.5.*
%{_mandir}/man5/mailx_event.conf.5.*
%{_mandir}/man5/report_mailx.conf.5.*
%{_mandir}/man*/reporter-mailx.*
%{_bindir}/reporter-mailx

%files plugin-ureport
%config(noreplace) %{_sysconfdir}/libreport/plugins/ureport.conf
%{_datadir}/%{name}/conf.d/plugins/ureport.conf
%{_bindir}/reporter-ureport
%{_mandir}/man1/reporter-ureport.1.gz
%{_mandir}/man5/ureport.conf.5.gz
%{_datadir}/%{name}/events/report_uReport.xml
%if 0%{?rhel} && ! 0%{?eln}
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_uReport.conf
%{_datadir}/%{name}/workflows/workflow_uReport.xml
%{_mandir}/man5/report_uReport.conf.5.*
%endif

%if %{with bugzilla}
%files plugin-bugzilla
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla.conf
%{_datadir}/%{name}/conf.d/plugins/bugzilla.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_format.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_formatdup.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_format_analyzer_libreport.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_formatdup_analyzer_libreport.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_format_kernel.conf
%{_datadir}/%{name}/events/report_Bugzilla.xml
%{_datadir}/%{name}/events/watch_Bugzilla.xml
%config(noreplace) %{_sysconfdir}/libreport/events/report_Bugzilla.conf
%config(noreplace) %{_sysconfdir}/libreport/events.d/bugzilla_event.conf
# FIXME: remove with the old gui
%{_mandir}/man1/reporter-bugzilla.1.gz
%{_mandir}/man5/report_Bugzilla.conf.5.*
%{_mandir}/man5/bugzilla_event.conf.5.*
%{_mandir}/man5/bugzilla.conf.5.*
%{_mandir}/man5/bugzilla_format.conf.5.*
%{_mandir}/man5/bugzilla_formatdup.conf.5.*
%{_mandir}/man5/bugzilla_format_analyzer_libreport.conf.5.*
%{_mandir}/man5/bugzilla_formatdup_analyzer_libreport.conf.5.*
%{_mandir}/man5/bugzilla_format_kernel.conf.5.*
%{_bindir}/reporter-bugzilla
%endif

%files plugin-mantisbt
%config(noreplace) %{_sysconfdir}/libreport/plugins/mantisbt.conf
%{_datadir}/%{name}/conf.d/plugins/mantisbt.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/mantisbt_format.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/mantisbt_formatdup.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/mantisbt_format_analyzer_libreport.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/mantisbt_formatdup_analyzer_libreport.conf
%{_bindir}/reporter-mantisbt
%{_mandir}/man1/reporter-mantisbt.1.gz
%{_mandir}/man5/mantisbt.conf.5.*
%{_mandir}/man5/mantisbt_format.conf.5.*
%{_mandir}/man5/mantisbt_formatdup.conf.5.*
%{_mandir}/man5/mantisbt_format_analyzer_libreport.conf.5.*
%{_mandir}/man5/mantisbt_formatdup_analyzer_libreport.conf.5.*

%files centos
%{_datadir}/%{name}/workflows/workflow_CentOSCCpp.xml
%{_datadir}/%{name}/workflows/workflow_CentOSKerneloops.xml
%{_datadir}/%{name}/workflows/workflow_CentOSPython.xml
%{_datadir}/%{name}/workflows/workflow_CentOSPython3.xml
%{_datadir}/%{name}/workflows/workflow_CentOSVmcore.xml
%{_datadir}/%{name}/workflows/workflow_CentOSXorg.xml
%{_datadir}/%{name}/workflows/workflow_CentOSLibreport.xml
%{_datadir}/%{name}/workflows/workflow_CentOSJava.xml
%{_datadir}/%{name}/workflows/workflow_CentOSJavaScript.xml
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_centos.conf
%{_mandir}/man5/report_centos.conf.5.*
%{_datadir}/%{name}/events/report_CentOSBugTracker.xml
%config(noreplace) %{_sysconfdir}/libreport/events/report_CentOSBugTracker.conf
%{_mandir}/man5/report_CentOSBugTracker.conf.5.*
# report_CentOSBugTracker events are shipped by libreport package
%config(noreplace) %{_sysconfdir}/libreport/events.d/centos_report_event.conf
%{_mandir}/man5/centos_report_event.conf.5.gz

%files plugin-reportuploader
%{_mandir}/man*/reporter-upload.*
%{_mandir}/man5/uploader_event.conf.5.*
%{_bindir}/reporter-upload
%{_datadir}/%{name}/events/report_Uploader.xml
%config(noreplace) %{_sysconfdir}/libreport/events.d/uploader_event.conf
%{_datadir}/%{name}/workflows/workflow_Upload.xml
%{_datadir}/%{name}/workflows/workflow_UploadCCpp.xml
%config(noreplace) %{_sysconfdir}/libreport/plugins/upload.conf
%{_datadir}/%{name}/conf.d/plugins/upload.conf
%{_mandir}/man5/upload.conf.5.*
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_uploader.conf
%{_mandir}/man5/report_uploader.conf.5.*
%config(noreplace) %{_sysconfdir}/libreport/events/report_Uploader.conf
%{_mandir}/man5/report_Uploader.conf.5.*

%if 0%{?fedora} || 0%{?eln}
%files fedora
%{_datadir}/%{name}/workflows/workflow_FedoraCCpp.xml
%{_datadir}/%{name}/workflows/workflow_FedoraKerneloops.xml
%{_datadir}/%{name}/workflows/workflow_FedoraPython.xml
%{_datadir}/%{name}/workflows/workflow_FedoraPython3.xml
%{_datadir}/%{name}/workflows/workflow_FedoraVmcore.xml
%{_datadir}/%{name}/workflows/workflow_FedoraXorg.xml
%{_datadir}/%{name}/workflows/workflow_FedoraLibreport.xml
%{_datadir}/%{name}/workflows/workflow_FedoraJava.xml
%{_datadir}/%{name}/workflows/workflow_FedoraJavaScript.xml
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_fedora.conf
%{_mandir}/man5/report_fedora.conf.5.*
%endif

%if 0%{?rhel} && ! 0%{?eln}
%files rhel-bugzilla
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaCCpp.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaKerneloops.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaPython.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaVmcore.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaXorg.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaLibreport.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaJava.xml
%{_datadir}/%{name}/workflows/workflow_RHELBugzillaJavaScript.xml
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/report_rhel_bugzilla.conf
%{_mandir}/man5/report_rhel_bugzilla.conf.5.*

%files rhel-anaconda-bugzilla
%{_datadir}/%{name}/workflows/workflow_AnacondaRHELBugzilla.xml
%endif

%if %{with bugzilla}
%files anaconda
%if 0%{?fedora} || 0%{?eln}
%{_datadir}/%{name}/workflows/workflow_AnacondaFedora.xml
%endif
%{_datadir}/%{name}/workflows/workflow_AnacondaUpload.xml
%config(noreplace) %{_sysconfdir}/libreport/workflows.d/anaconda_event.conf
%config(noreplace) %{_sysconfdir}/libreport/events.d/bugzilla_anaconda_event.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_format_anaconda.conf
%config(noreplace) %{_sysconfdir}/libreport/plugins/bugzilla_formatdup_anaconda.conf
%{_mandir}/man5/anaconda_event.conf.5.*
%{_mandir}/man5/bugzilla_anaconda_event.conf.5.*
%{_mandir}/man5/bugzilla_format_anaconda.conf.5.*
%{_mandir}/man5/bugzilla_formatdup_anaconda.conf.5.*
%endif

%changelog
* Mon Jan 17 2022 Matěj Grabovský <mgrabovs@redhat.com> 2.16.0-1
- spec: Bump satyr dependency to 0.38 (mgrabovs@redhat.com)
- lib: Bump library revision (mgrabovs@redhat.com)
- event_xml_parser: Use g_steal_pointer() where appropriate
  (mgrabovs@redhat.com)
- event_xml_parser: Purge commented-out code (mgrabovs@redhat.com)
- curl: Do not autofree header string (mgrabovs@redhat.com)
- reporter-upload: Use g_steal_pointer() (mgrabovs@redhat.com)
- bugzilla: Initialize autofree variables to NULL (mgrabovs@redhat.com)
- bugzilla: Drop global_uuid (mgrabovs@redhat.com)
- report-python: Remove commented-out code (mgrabovs@redhat.com)
- Update translations (mgrabovsky@users.noreply.github.com)
- Update translations (mgrabovsky@users.noreply.github.com)
- Update translations (mgrabovs@redhat.com)
- debuginfo-install is a dnf command (ckujau@users.noreply.github.com)
- tests: Don't use deprecated assertEquals (mfabik@redhat.com)
- ignored_words: Add DEBUGINFOD_URLS env var (mfabik@redhat.com)
- [rhbz] Retry XML-RPC calls when uploading attachments (michal@redhat.com)
- Update changelog (michal@redhat.com)
- [rhbz] Be a little bit more defensive when working with subcomponents
  (michal@redhat.com)
- Update changelog (mfabik@redhat.com)
- spec: Drop libproxy dependency (mgrabovs@redhat.com)
- Avoid direct use of libproxy (mcatanzaro@redhat.com)
- changelog: Fix links to commits (mgrabovs@redhat.com)
- Use g_free for GLib-alloc'd memory (mfabik@redhat.com)

* Tue Jun 01 2021 Michal Fabik <mfabik@redhat.com> 2.15.2-1
- binhex: Remove unused API (mgrabovs@redhat.com)
- lib: Use GLib for computing SHA-1 digests (mgrabovs@redhat.com)
- run_event: Improve memory management (mgrabovs@redhat.com)
- gtk: Fix segfault (mgrabovs@redhat.com)

* Tue May 04 2021 Michal Fabik <mfabik@redhat.com> 2.15.1-1
- ureport: Strange usage of tmp variable (mzidek@redhat.com)
- steal_directory: Silence a warning (mzidek@redhat.com)
- dump_dir: Use g_free and re-init to NULL (mzidek@redhat.com)
- dump_dir: Use g_free instead of free (mzidek@redhat.com)
- dirsize: No need to check for NULL (mzidek@redhat.com)
- dirsize: Bad checks for NULL (mzidek@redhat.com)
- gui-wizard-gtk: Possible double free (mzidek@redhat.com)
- gui-wizard-gtk: Check if EXCLUDE_FROM_REPORT is set (mzidek@redhat.com)
- gui-wizard-gtk: Improve docs and add missing free (mzidek@redhat.com)
- cli: Address of local auto-variable assigned to a function parameter
  (mzidek@redhat.com)
- gtk-helpers: Add missing g_strfreev() (mzidek@redhat.com)
- cli: Add missing g_free call (mzidek@redhat.com)
- gitignore: Drop misleading comment (mfabik@redhat.com)
- spec: Sync upstream with distgit (mfabik@redhat.com)
- changelog: Add missing link to changes in 2.15.0 (mgrabovs@redhat.com)
- spec: Make plugin-mailx depend on /usr/bin/mailx (mgrabovs@redhat.com)
- Add support for excluding whole elements from search for sensitive words
  (michal@redhat.com)
- ignored_words: add more "key" variations (michal@redhat.com)

* Thu Mar 18 2021 Michal Fabik <mfabik@redhat.com> 2.15.0-1
- tito: Clean up tito setup (mfabik@redhat.com)
- Revert "Automatic commit of package [libreport] release [2.15.0-1]."
  (mfabik@redhat.com)
- .tito: Change commit message and tag format (mfabik@redhat.com)
- Automatic commit of package [libreport] release [2.15.0-1].
  (mfabik@redhat.com)
- Update changelog (mfabik@redhat.com)
- Update version info before release (mfabik@redhat.com)
- reportclient: Use set instead of list to store packages (mgrabovs@redhat.com)
- reportclient: Refactoring for maintainablility (mgrabovs@redhat.com)
- reportclient: Improve logic and Pythonic style (mgrabovs@redhat.com)
- reportclient: Add typing hints (mgrabovs@redhat.com)
- reportclient: Code style fixes (mgrabovs@redhat.com)
- CHANGELOG: Update unreleased (mfabik@redhat.com)
- Replace libtar with libarchive (mfabik@redhat.com)
- [spec] Drop AnacondaRHEL workflow reference (michal@redhat.com)
- spec: Drop trailing comment (mfabik@redhat.com)
- gtk-helpers: Check for NULL before saving event config data
  (mgrabovs@redhat.com)
- reporter-bugzilla: Fix bad error detection (mzidek@redhat.com)
- get_cmdline: Ordered comparison with a pointer (mzidek@redhat.com)
- BR make (msuchy@redhat.com)
- reporter-ureport: Untranslatable string (mzidek@redhat.com)
- reporter-upload: Untranslatable string (mzidek@redhat.com)
- reporter-mailx: Untranslatable string (mzidek@redhat.com)
- reporter-mantisbt: Untranslatable string (mzidek@redhat.com)
- reporter-bugzilla: Untranslatable string (mzidek@redhat.com)
- spec: Drop subscription-manager-rhsm dependency (mfabik@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- apidoc: Update Doxyfile (mzidek@redhat.com)
- README: Reference ABRT in the README (mzidek@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- spec: Use autoreconf in %%build instead of autoconf (mgrabovs@redhat.com)
- wizard: Fix -Wincompatible-pointer-types (mfabik@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- rhbz: Fix a double-free condition (mgrabovs@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- gui-wizard-gtk: Don't autofree URL string (mgrabovs@redhat.com)
- gui-wizard-gtk: Compare pointer to NULL, not 0 (mgrabovs@redhat.com)
- event_config: Null autofree pointers before returning (mgrabovs@redhat.com)
- gui-wizard-gtk: Fix segfault (mgrabovs@redhat.com)
- gui-wizard-gtk: Fix a segfault and memory leak (mgrabovs@redhat.com)
- gui-wizard-gtk: Fix a double free condition (mgrabovs@redhat.com)
- ureport: Fix segfault (mgrabovs@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- Simplify malloc arguments (mfabik@redhat.com)
- Use g_clear_pointer to free and NULL (mfabik@redhat.com)
- tests: Suppress valgrind errors from xmlrpc code (mfabik@redhat.com)
- tests: rhbz_functions: Use more rhbz_ functions (mfabik@redhat.com)
- tests: mock-bz: Add creator to comments (mfabik@redhat.com)
- tests/rhbz_functions: Fix leaks (mfabik@redhat.com)
- rhbz: Fix leaks (mfabik@redhat.com)
- get_cmdline: Prevent write beyond end of string (mfabik@redhat.com)
- uriparser: Free regex_t struct (mfabik@redhat.com)
- dump_dir: Prevent read before beginning of array (mfabik@redhat.com)
- event_xml_parser: Fix typo in comment (mfabik@redhat.com)
- event_xml_parser: Refactor condition (mfabik@redhat.com)
- event_xml_parser: Fix leaks (mfabik@redhat.com)
- dump_dir: Fix leaks (mfabik@redhat.com)
- configuration_files: Fix leaks (mfabik@redhat.com)
- ureport: Fix leaks (mfabik@redhat.com)
- ureport: Use g_free to avoid freeing NULL (mfabik@redhat.com)
- glib_support: Clean up properly after g_strsplit (mfabik@redhat.com)
- tests: Drop unused variables (mfabik@redhat.com)
- make_description.at: Fix grammar (mfabik@redhat.com)
- tests: Fix leaks (mfabik@redhat.com)
- tests: Fix -Wimplicit-function-declaration (mfabik@redhat.com)
- tests: Fix -Wdiscarded-qualifiers (mfabik@redhat.com)
- tests: Only fail on definite and indirect leaks (mfabik@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- client-python: Add getter for package count to downloader
  (mgrabovs@redhat.com)
- github: Drop build workflow (ekulik@redhat.com)
- github: Add workflow for creating releases (ekulik@redhat.com)
- Add Packit configuration (ekulik@redhat.com)
- libreport.spec: Drop changelog (ekulik@redhat.com)
- add releasers (msuchy@redhat.com)
- Use tito for releasing (msuchy@redhat.com)
- Initialized to use tito. (msuchy@redhat.com)
- ignored_words: Ignore "bypass" and "IBRS_FW" (mgrabovs@redhat.com)
- reporter-mailx: Use GLib facilities for pointer arrays (mgrabovs@redhat.com)
- lib: Auto-cleanup for problem structs (mgrabovs@redhat.com)
- gui-wizard-gtk: wizard: Fix invalid memory read (ekulik@redhat.com)
- gui-wizard-gtk: wizard: Remove variable (ekulik@redhat.com)
- Update translations (jenkins@jenkins-csb-abrt.svc)
- gtk-helpers: desktop-utils: Refactor helper function (ekulik@redhat.com)
- Updates so ELN builds in a Fedora-like reporting configuration, even though
  the %%{rhel} macro is set. (mmathesi@redhat.com)
- spec: drop %%{?_isa} from BuildRequires (mfabik@redhat.com)
- testsuite: Change var name to prevent shadowing (mfabik@redhat.com)
- tests: Add tests for rhbz_ functions (mfabik@redhat.com)
- tests: Add sample problem dir (mfabik@redhat.com)
- tests: Add mock Bugzilla server (mfabik@redhat.com)
- rhbz: rhbz_mail_to_cc: Fix xmlrpc format string (mfabik@redhat.com)
- rhbz: Use int array as 'ids' arg to Bug.update (mfabik@redhat.com)
- rhbz: Add sub_component to xmlrpc params (mfabik@redhat.com)
- rhbz: Add function to pick default subcomponent (mfabik@redhat.com)
- rhbz: Add function to get subcomponents from BZ (mfabik@redhat.com)
- xmlrpc: Change misleading function names (mfabik@redhat.com)

