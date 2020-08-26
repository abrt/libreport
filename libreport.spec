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
Version: 2.14.0
Release: 1%{?dist}
License: GPLv2+
URL: https://abrt.readthedocs.org/
# to generate tarball:
# git clone https://github.com/abrt/libreport.git
# cd libreport
# git reset --hard %%{name}-%%{version}
# tito build --tgz
Source: %{name}-%{version}.tar.gz
BuildRequires: %{dbus_devel}
BuildRequires: gtk3-devel
BuildRequires: curl-devel
BuildRequires: desktop-file-utils
BuildRequires: python3-devel
BuildRequires: gettext
BuildRequires: libxml2-devel
BuildRequires: libtar-devel
BuildRequires: intltool
BuildRequires: libtool
BuildRequires: texinfo
BuildRequires: asciidoc
BuildRequires: xmlto
BuildRequires: newt-devel
BuildRequires: libproxy-devel
BuildRequires: satyr-devel >= 0.24
BuildRequires: glib2-devel >= %{glib_ver}
BuildRequires: nettle-devel

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
Requires: satyr%{?_isa} >= 0.24
Requires: glib2%{?_isa} >= %{glib_ver}
Requires: libarchive%{?_isa}
Requires: nettle%{?_isa}

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
Requires: mailx

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
%if 0%{?rhel} && ! 0%{?eln}
Requires: python3-subscription-manager-rhsm
%endif

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
sed 's|DEF_VER=.*$$|DEF_VER='%{version}'|' -i gen-version
#./gen-version
./autogen.sh
autoconf

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
%endif # with python3
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
%{_datadir}/%{name}/conf.d/libreport.conf
%{_libdir}/libreport.so.*
%{_mandir}/man5/libreport.conf.5*
%{_mandir}/man5/report_event.conf.5*
%{_mandir}/man5/forbidden_words.conf.5*
%{_mandir}/man5/ignored_words.conf.5*
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
%if 0%{?rhel} && ! 0%{?eln}
%{_datadir}/%{name}/workflows/workflow_AnacondaRHEL.xml
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
* Wed Aug 19 2020 Merlin Mathesius <mmathesi@redhat.com> - 2.14.0-1
- Updates so ELN builds in a Fedora-like reporting configuration, even though
  the %%{rhel} macro is set.

* Thu Aug 13 2020 Michal Fabik <mfabik@redhat.com> 2.14.0-1
- Update version-info before new release
- Purge commented-out code
- lib: Decommission libreport_list_free_with_free
- cli,lib,gui-wizard-gtk: Fix segfaults
- gui-wizard-gtk: Fix typo in function name
- forbidden_words: Add potentially sensitive env vars
- Update translations
- lib: compress: Use libarchive
- Remove remnants of Red Hat Customer Portal support
- lib,reporter-mantisbt: Fix memory leaks
- cli-report: Fix a double-free condition
- reporter-systemd-journal: Fix a double-free
- tests: Replace salacious strings
- Drop Red Hat Customer Portal reporter
- tests: Include test data in the distribution tarball
- gitignore: Add test-related files
- tests: Rewrite (de)compression test
- github: check: Don’t use autogen.sh sysdeps
- spec: Use architecture-dependent dependencies
- lib: Fix typo in error message
- lib: Check for overflow properly
- lib: Fix potential null pointer dereference
- lib: Check for errors when opening files
- gtk-helpers: Check return value
- lib: Fix resource leaks reported by Coverity
- rhbz: Wrap long lines
- tests: Skip Bugzilla tests with --without-bugzilla
- lib: Don’t use external executables for decompression
- ureport: Drop Strata integration
- Add GitHub Actions workflow for building PRs
- lib: event_config: Don’t free internal GLib string
- autogen.sh: Pass --assumeyes to dnf
- gtk,lib: Update symbol list
- Use g_autoptr where possible
- Remove unused ato[iu] functions
- Remove GHashTable aliases
- Remove redundant search_item functions
- Replace libreport_ureport_from_dump_dir with libreport_ureport_from_dump_dir_ext
- Use g_autofree where possible
- Remove unused libreport_try_get_map_string_item_as_int
- Remove unused libreport_set_map_string_item_from_int
- Remove unused libreport_try_get_map_string_item_as_uint
- Remove unused libreport_set_map_string_item_from_uint
- Remove unused libreport_try_get_map_string_item_as_string
- Remove unused libreport_set_map_string_item_from_string_vector
- Remove unused libreport_try_get_map_string_item_as_string_vector
- Remove unused libreport_set_map_string_item_from_bool
- Replace libreport_string_vector_new_from_string with g_strsplit
- Replace libreport_string_vector_free with g_strfreev
- Remove unused libreport_size_map_string
- Replace insert_map_string with g_hash_table_insert
- Replace libreport_set_map_string_item_from_string with g_hash_table_replace
- Replace libreport_replace_map_string_item with g_hash_table_replace
- Replace libreport_next_map_string_iter with g_hash_table_iter_next
- Replace libreport_remove_map_string_item with g_hash_table_remove
- Replace libreport_get_map_string_item_or_NULL with g_hash_table_lookup
- Replace libreport_init_map_string_iter with g_hash_table_iter_init
- Replace libreport_get_map_string_item_or_empty with g_hash_table_lookup
- Replace libreport_free_map_string with g_hash_table_destroy
- Replace libreport_new_map_string with g_hash_table_new_full
- Remove unused libreport_xstat
- Replace libreport_xpipe with g_unix_open_pipe
- Replace libreport_xchdir with g_chdir
- Replace libreport_concat_path_file with g_build_filename
- Remove unused strbuf_grow
- Replace strbuf with GString
- Replace libreport_suffixcmp with g_str_has_suffix
- Replace libreport_prefixcmp with g_str_has_prefix
- Replace g_malloc with g_new for structs
- Remove unused libreport_xunlink
- Replace libreport_xopen with g_open
- Replace libreport_xzalloc with g_malloc0
- Replace libreport_xrealloc with g_realloc
- Replace libreport_xmalloc with g_malloc
- Replace libreport_try_atou with g_ascii_strtoull
- Replace libreport_xatou with g_ascii_strtoull
- Replace libreport_xatoi_positive with g_ascii_strtoull
- Replace libreport_xatoi with g_ascii_strtoll
- Replace libreport_strtrim with g_strstrip
- Replace libreport_xstrndup with g_strndup
- Replace libreport_xstrdup with g_strdup
- Replace libreport_xasprintf with g_strdup_printf
- lib: Add version script for libreport
- git: Add some binaries to gitignore
- doc: Exclude more files with --without-bugzilla
- internal: Remove useless #define
- lib: Remove creates-items tag parsing in event definitions
- dd: Update dd_get_owner to handle error return values
- dirsize: Fix leaks
- dirsize: Don't pick new dir for deletion
- dirsize: Count size of dir with sosreport.log
- dirsize: Don't pick .lock'd dirs for deletion
- Fix -Wmissing-field-initializers across libreport
- setgid instead of setuid the abrt-action-install-debuginfo-to-abrt-cache [RHBZ 1796245]

* Mon May 11 2020 Michal Fabik <mfabik@redhat.com> 2.13.1-1
- Update translations
- Fix a search-and-replace error
- Document commit message template
- Add a commit message template
- rhbz.c: Remove dead link to documentation
- README: Fix link to FAF instance
- spec: Don’t add -Werror to CFLAGS
- spec: Use macros for make invocations
- spec: Use %autosetup

* Thu Apr 23 2020 Michal Fabik <mfabik@redhat.com> 2.13.0-1
- Remove unused zanata.xml
- Remove obsolete pull-translations
- Allow editing CHANGELOG.md before release commit
- configure.ac: use POSIX expr in place of bashisms
- lib: Drop D-Bus code
- Fix build_ids_to_paths function name
- Update translations
- workflow: Fix namespacing with #defines
- ureport: Fix namespacing with #defines
- run_event: Fix namespacing with #defines
- reporters: Fix namespacing with #defines
- libreport_types: Fix namespacing with #defines
- libreport_curl: Fix namespacing with #defines
- internal_libreport: Fix namespacing with #defines
- global_configuration: Fix namespacing with #defines
- dump_dir: Fix namespacing with #defines
- client: Fix namespacing with #defines
- internal_libreport_gtk: Fix namespacing with #defines
- internal_libreport: Sort kernel namespaces
- Support new kernel namespace
- gui-wizard-gtk: Wrap event log messages
- client-python: Accomodate for multiple debug directories
- plugins: report: Add “strata” target back
- plugins: rhtsupport.conf: Replace “strata” with “rhts”
- plugins: reporter-rhtsupport: Drop unused debugging code
- plugins: report: Use “rhts” instead of “strata”

* Thu Feb 06 2020 Michal Fabik <mfabik@redhat.com> 2.12.0-1
- ureport: Allow printf-like data attaching
- plugins: reporter-rhtsupport: Avoid runtime warning
- Update translations
- lib: Don't include Nettle in a public interface
- ureport: Drop HTTP header table
- Update (uk) translation
- Update (de) translation
- Prefer sr_Latn over sr@latin
- Update (tr) translation
- Update (sk) translation
- Update (pt_BR) translation
- Update (hu) translation
- Update (fr) translation
- Update (bg) translation
- Translated using Weblate (Polish)
- Add (zh_HK) translation
- glib_support: Use g_strsplit
- glib_support: Drop type initialization
- Remove translation skeletons
- client-python: Drop yumdebuginfo
- Add pot file
- lib: Use Nettle for computing SHA-1 digests
- Move augeas lenses to new subdirectory

* Thu Nov 14 2019 Matěj Grabovský <mgrabovs@redhat.com> 2.11.3-1
- Remove unused scripts
- gtk: Fix infinite loop crashing the reporting
- gtk: Improve logging
- gtk: Remove page number from page struct
- gtk: Code style adjustments
- Make notebook tabs invisible again
- gui-wizard-gtk: Remove expert mode
- gui-wizard-gtk: Stop allowing overriding UI definitions
- pull-trans: Suggest zanata install
- shellcheck: Iterating over ls output is fragile. Use globs.
- shellcheck: Double quote to prevent globbing and word splitting
- zanata: Use python3 zanata client to pull translations
- gtk: Fix another possible double-free

* Wed Oct 23 2019 Matěj Grabovský <mgrabovs@redhat.com> 2.11.2-1
- gtk: Improve memory management
- gtk: Prevent memory leak
- lib: Eliminate GLib inefficiency
- gtk,style: Minor style consistency fixes
- workflows: Correct name of post_report event

* Wed Oct 16 2019 Matěj Grabovský <mgrabovs@redhat.com> 2.11.1-1
- gtk: Fix a double-free condition

* Fri Oct 11 2019 Matěj Grabovský <mgrabovs@redhat.com> 2.11.0-1
- Remove option for emergency analysis/reporting
- tests: proc_helpers: Fix call to errx()
- plugins: bugzilla: Add format file for libreport duplicates
- dbus: Remove interface introspection files
- lib: Don't warn if a configuration key is missing
- gtk: Handle event wildcards in command line options
- gtk: Better handling of workflows with wildcarded events
- lib: Remove unused arguments of prepare_commands
- lib: Reintroduce error logging in event XML parser
- cli: Continue running even if some events have no commands
- cli: Expand event name wildcards
- lib: Expand wildcards in workflow XML parser
- lib: Add a function to expand wildcards in event names
- style: Simplify code; fix typos in comments
- gitignore: Update with missing and renamed generated files
- dirsize: Skip dirs in which sosreport is being generated
- tests: Fix Python tests being skipped unconditionally
- Remove Python 2 support

* Wed Jul 03 2019 Martin Kutlak <mkutlak@redhat.com> 2.10.1-1
- doc: Makefile.am: Use correct path for --conf-file
- lib: copy_file_recursive: Use GLib abstractions
- gui-wizard-gtk: Fix fix
- cli: run-command: Replace use of vfork() with fork()
- plugins: rhbz: Don’t call strlen() on attachment data
- Check for empty fmt_file name when printing error msgs
- cli: Unpack command-line argument parsing logic
- lib: event_config: Remove pointless assignment
- gui-wizard-gtk: Fix never-read assignment
- lib: xatonum: Check string parameters
- Rework and refine composition of error messages
- Add clearer warnings about missing report elements specified in format files
- Move uReport workflow to plugin-ureport subpackage
- lib: ureport: Export workflow when saving server response
- lib: dump_dir: Clean up on failure in dd_delete()
- Use #ifdef to check whether macros are defined
- autogen.sh: Use autoreconf
- autogen.sh: Allow skipping running configure
- tests: forbidden_words: Don’t hardcode sysconfdir
- Makefile.am: Use correct locale when getting date

* Sat Feb 02 2019 Ernestas Kulik <ekulik@redhat.com> 2.10.0-1
- Update to 2.10.0

* Fri Dec 07 2018 Matej Marusak <mmarusak@redhat.com> 2.9.7-1
- reportclient: Search for required packages recursively
- event_config: Modify unusable backtrace message
- reportclient: Find and download required debuginfo packages
- lib: Explicitly do not use DST
- autogen: List make in sysdeps command
- lib: Seek beginning of mountinfo file
- report-client: Find debuginfos in own method
- lib: Add a null guard
- gui-wizard-gtk: Require GTK+ 3.10
- gui-wizard-gtk: Don’t set GtkButton:xalign
- gui-wizard-gtk: Replace use of GtkTable
- gui-wizard-gtk: Show warnings inline on progress page
- gui-wizard-gtk: Remove unused size group
- gui-wizard-gtk: Replace Gtk{H,V}Box with GtkBox
- gui-wizard-gtk: Remove unneeded windows
- gtk-helpers: config_dialog: Hide tree view header
- augeas: Use generic augeas modules

* Fri Oct 05 2018 Martin Kutlak <mkutlak@redhat.com> 2.9.6-1
- Translation updates
- tests: Adjust format of truncated backtrace for python and core
- spec: Make libreport-filesystem subpackage noarch
- coverity: Remove deadcode #def47
- coverity: Remove reverse inull #def30
- coverity: Check return value of fstat call #def31
- coverity: Remove check for null pointer with no effect #def33
- coverity: Check null pointer before dereferencing it #def35
- coverity: Change data type for bug_id variable #def[44,43]
- coverity: Check if pointer isnt null before strcmp #def40
- coverity: Free resource leaking vars #def[42,41,38,37]
- coverity fixes [#def21]
- coverity fixes [#def16] [#def17]
- coverity fix [#def9]
- coverity fix [#def7]
- coverity fix [#def6]
- gui: Replace deprecated g_type_class_add_private
- potfiles: fix issue in POTFILES.in
- python_sitearch -> python2_sitearch
- Remove option to screencast problems
- lib: fix a SEGV in list_possible_events()
- ureport: use python3 to get consumerCertDir
- spec: set PYTHON variable because of ./py-compile
- r-mailx: Add comment explaining expected values in config
- reporter-mailx: Remove double quotes from config
- spec: drop dependency on python-rhsm

* Wed Apr 25 2018 Matej Habrnal <mhabrnal@redhat.com> 2.9.5-1
- build: create report sub-directory
- testcase: port python's tests to python3
- spec: actualize according to downstream
- spec: Conditionalize the Python2 and Python3
- report-python: fix tests if configure --without-python2
- autogen: correctly parse buildrequires from spec file

* Tue Mar 27 2018 Martin Kutlak <mkutlak@redhat.com> 2.9.4-1
- Translation updates
- Revert "use /usr/sbin/"
- ureport: remove json-c is_error() usage
- ldconfig and gtk-update-icon-cache is not needed in rawhide
- reporter-rhtsupport: Remove dependency on redhat-access-insights
- do not expand macro in changelog
- move defattr which match the defaults
- use /usr/sbin/
- macro python_sitearch is always defined on rhel7+
- remove rhel6 specific items and accomodate to rhel7+
- This package uses names with ambiguous `python-` prefix in requirements.
- reporter-{bugzilla,mantisbt,rhtsupport}: fix free
- reporter-mailx: rely on configured email
- spec: fix unowned directories
- augeas: include local config path
- doc: update to contain newly added user's local config
- reporter-mantisbt: read configuration from user's home
- reporter-rhtsupport: read configuration from user's home
- reporter-bugzilla: read configuration from user's home
- reporter-bugzilla: ask concrete bz when requiring login
- makefile: fix make release

* Thu Nov 02 2017 Julius Milan <jmilan@redhat.com> 2.9.3-1
- Translation updates
- commit to delete
- workflows: fix description in workflow_RHELJavaScript.xml.in
- workflows: add workflow for adding data to existing case
- client-python,report-python: Allow python to be optional at build time
- ignored words: add SYSTEMD_NSS_BYPASS_BUS
- reporter-ureport: add 'ProcessUnpackaged' option
- spec: add workflow for adding data to existing case
- rep-sys-journal: fix in finding executable basename
- remove old obsolete
- Group is not used any more
- remove old changelogs
- requires pythonX-dnf instead of dnf
- doc: fix obsolete doxygen tags & complains
- lib: Introduce pid_for_children element from ns
- client-python: Do not try to unlink None
- spec: rename Python binary packages

* Thu Mar 16 2017 Matej Habrnal <mhabrnal@redhat.com> 2.9.1-1
- build: create tarball in release-* target
- problem_data: fix double const
- wizard: fix error found by -Werror=format-security
- run_event: fix cmp between pointer and zero character
- build: do not upload tarball to fedorahosted.org
- spec: do not use fedorahosted.org as source
- build: fix generating list of dependences in autogen.sh
- build: generate new release entries with date
- report-newt: free allocated variables, don't close dd twice
- build: fix scratch-build target
- changelog: reflect the PR
- lib: several bug fixes in parsing of mountinfo
- lib: correctly recognize chroot in container
- lib: declare CONTAINER_ROOTS element name
- lib: add more log wrappers for perror
- reporter-bugzilla: use /etc/os-release for default url
- configure.ac: Remove nss dependency
- spec: include testsuite headers in the devel package
- tests: include testsuite.h in the dist archive
- maint: check pulled .po files for errors
- build: fix bug in changelog generating in release target
- changelog: fix typos

* Fri Dec 02 2016 Jakub Filak <jakub@thefilaks.net> 2.9.0-1
- Translation updates
- build: make the release-* targets smarter
- add CHANGELOG.md
- reporter-s-journal: enable SYSLOG_IDENTIFIER from env
- report-python: add method run_event_on_problem_dir
- lib: use lz4 instead of lz4cat
- reportclient: honor ABRT_VERBOSE
- tree-wide: introduce 'stop_on_not_reportable' option
- client: add support for $releasever to debuginfo
- lib: correct test for own root
- workflows: run analyze_BodhiUpdates event on Fedora
- man: fix formating
- reporter-systemd-journal: introduce reporter-systemd-journal
- problem_data: add function which returns all problem data keys
- include: add exception_type element constant
- spec: changes related to reporter-systemd-journal
- problem_report: add normalization of crashed thread
- problem_report: make generate report configurable
- problem_report: use core_backtrace if there is no backtrace
- lib: refuse to parse negative number as unsigned int
- spec: simplify and remove old conditional
- build: add gettext-devel to sysdeps
- dd: add check for validity of new file FD
- build: configure tree for debugging by default
- spec: use %%buildroot macro
- spec: remove defattr which match the defaults
- spec: do not clean buildroot
- spec: remove Groups
- spec: code cleanup
- lib: fix a bug in dealing with errno
- lib: add convenient wrappers for uint in map_string_t
- problem_report: ensure C-string null terminator
- lib: fix invalid cgroup namespace ID
- lib: make die function configurable
- lib: allow using FD of /proc/[pid] instead of pid_t
- dd: add functions for opening dd item
- lib: add xfdopen
- problem data: search for sensitive words in more files
- dd: add dd_copy_file_at
- ignored words: add "systemd-logind" and "hawkey"
- build: reset the default version with each release
- doc: make README more verbose
- tree-wide: produce less messages in NOTICE log lvl
- ureport: less confusing logging
- spec: install JavaScript workflows
- workflow: add JavaScript workflows
- bugzilla: stop including package details

* Fri Sep 09 2016 Jakub Filak <jfilak@redhat.com> 2.8.0-1
- lib: fix a memory leak in create_dump_dir fn
- rhtsupport: fix a double free of config at exit
- autogen: fix typo in usage help string
- debuginfo: dnf API logging workarounds list
- lib: don't warn when user word file doesn't exist
- testuite: add test for forbidden_words
- lib: be able to define base conf dir at runtime
- wizard: use dnf instead of yum in add a screencast note
- problem_report: document resevered elements

* Mon Jul 18 2016 Matej Habrnal <mhabrnal@redhat.com> 2.7.2-1
- Translation updates
- wizard: do not create reproducible if complex_detail == no
- include: save_user_settings function declaration isn’t a prototype
- Bugzilla: fix typo in comment don -> don't
- client-python: fix a typo in error check
- dd: do not log missing uid file when creating new dump dir
- build: update searched pkg names for systemd

* Wed May 18 2016 Matej Habrnal <mhabrnal@redhat.com> 2.7.1-1
- spec: compression updates
- lib: add lz4 decompression
- lib: avoid the need to link against lzma
- all: format security
- lib: add cgroup namespace
- dd: introduce functions getting occurrence stamps
- dd: introduce dd_get_env_variable
- lib: add get env variable from a file
- RHTSupport: include count in Support cases
- lib: problem report API check fseek return code
- ignored words: remove 'kwallet_jwakely' which I added wrongly

* Fri Apr 08 2016 Matej Habrnal <mhabrnal@redhat.com> 2.7.0-1
- ignored words: update ignored words
- mailx: introduce debug parameter -D
- mailx: mail formatting: add comment right after %%oneline
- mailx: use problem report api to define an emais' content
- lib: remove unused function make_description_bz
- augeas: trim spaces before key value
- Revert "xml parser: be more verbose in case xml file cannot be opened"
- xml parser: be more verbose in case xml file cannot be opened
- spec: add workflows.d to filesystem package
- makefile: define LANG in release target
- mailx: stop creating dead.letter on mailx failures
- workflows: add comments to ambiguous functions
- workflows: NULL for the default configuration dir
- workflows: publish the function loading configuration
- build: fix build on Fedora24
- augeas: exclude mantisbt format configurations
- reporter-mantisbt: add missing '=' to conf file
- curl: fix typo Ingoring -> Ignoring
- rhtsupport: attach all dump dir's element to a new case
- rhtsupport: add pkg_vendor, reproducer and reproducible to description
- report client: add silent mode to clean_up()
- doc: add documentation for requires-details attribute
- rhtsupport: Discourage users from reporting in non Red Hat stuff
- rhtsupport: Discourage users from opening one-shot crashes
- report-gtk: Require Reproducer for RHTSupport
- Add workflow for RHEL anonymous report
- spec: add workflow for RHEL anonymous report files
- wizard: fix the broken widget expansion
- dd: add documentation of dd_create_skeleton
- workflow: add extern C to the header file
- Fix minor typos
- Translation updates
- translations: update zanata configuration
- wizard: fix the broken "Show log" widget
- wizard: remove the code correcting Bugzilla groups

* Tue Feb 02 2016 Matej Habrnal <mhabrnal@redhat.com> 2.6.4-1
- doc: add option -o and -O into reporter-ureport man page
- rhtsupport: use problme report API to create description
- bugzilla: make the event configurable
- report-gtk: offer users to create private ticket
- bugzilla|centos: declare 'restricted access' support
- event config: add support for 'restricted access'
- lib: move CREATE_PRIVATE_TICKET to the global configuration
- dd: dd_delete_item does not die
- dd: add function getting stat of item
- dd: correct handling of TYPE when creating dump directory
- dd: add function computing dump dir file system size
- dd: add function counting number of dd items
- dd: add function copying file descriptor to element
- dd: allow 1 and 2 letter long element names
- problem_data: factor out function reading single problem element
- formatdup: more universal comment
- dd: make function uid_in_group() public
- Refactoring conditional directives that break parts of statements.
- bugzilla: actualize man pages
- bugzilla: don't report private problem as comment
- uploader: move username and password to the advanced options
- uploader: allow empty username and password
- spec: add uploader config files and related man page
- uploader: add possibility to set SSH keyfiles
- curl: add possibility to configure SSH keys
- desktop-utils: deal with Destkop files without command line
- ureport: enable attaching of arbitrary values
- update .gitignore
- uploader: save remote name in reported_to
- curl: return URLs without userinfo
- lib: add function for removing userinfo from URIs
- plugins: port reporters to add_reported_to_entry
- reported_to: add a function formatting reported_to lines
- lib: introduce parser of ISO date strings
- uploader: use shared dd_create_archive function
- dd: add a function for compressing dumpdirs
- problem_report: add examples to the documentation
- client: document environment variables

* Thu Oct 15 2015 Matej Habrnal <mhabrnal@redhat.com> 2.6.3-1
- wizard: correct comments in save_text_if_changed()
- events: improve example
- reporter-bugzilla: add parameter -p
- wizard: fix save users changes after reviewing dump dir files
- dd: make function load_text_file non-static
- bugzilla: don't attach build_ids
- run_event: rewrite event rule parser
- dd: add convenience wrappers fro loading numbers
- ureport: improve curl's error messages
- ureport: use Red Hat Certificate Authority to make rhsm cert trusted
- curl: add posibility to use own Certificate Authority cert
- spec: add redhat-access-insights to Requires of l-p-rhtsupport
- bugzilla: put VARIANT_ID= to Whiteboard
- autogen: use dnf instead of yum to install dependencies
- configure: use hex value for dump dir mode
- curl: add a helper for HTTP GET
- dd: don't warn about missing 'type' if the locking fails
- dd: stop warning about corrupted mandatory files
- Use a dgettext function returning strings instead of bytes
