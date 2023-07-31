# Change Log
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]
### Fixed
- reporter-upload: Fix double free crash (#792)
- gui-wizard-gtk: Enable word wrap for comments

## [2.17.11] - 2023-06-30
### Fixed
- reporter-bugzilla: Fix NameError exception
- reporter-bugzilla: Adding an attachment is a minor bug update (skip emails)
- reporter-bugzilla: Retry Bugzilla queries with delays, if needed

### Added
- Add "XDG_ACTIVATION_TOKEN" to the list of ignored words

## [2.17.10] - 2023-05-11
### Fixed
- reporter-upload: Fix use-after-free error (#792)

## [2.17.9] - 2023-03-24
### Fixed
- reporter-bugzilla: Honor XDG_CONFIG_HOME when accessing user's configuration files (Thanks to Yann Droneaud)
- reporter-bugzilla: Fix reporting when the bug already exists
- reporter-bugzilla: Don't fail if reported_to file doesn't exist (rhbz#2176481)
- reporter-bugzilla: Make sure that the creator of a bug is always in CC
- reporter-bugzilla: Don't send emails about each individual attachment (rhbz#2180286)
- reporter-bugzilla: Fix string interpolation for duplicate bugs

## [2.17.8] - 2023-03-03
### Fixed
- reporter-bugzilla: Fix password prompt in client/server mode (rhbz#2172891)
- reporter-bugzilla: Fix KeyError when HOME env var is not set

## [2.17.7] - 2023-02-20
### Fixed
- reporter-bugzilla: Fix TypeError (rhbz#2140799)
- spec: f38+ should use the python implementation of reporter-bugzilla by default (rhbz#2170048)

## [2.17.6] - 2022-11-06
### Fixed
- reporter-bugzilla: Fix TypeError (rhbz#2137472)

## [2.17.5] - 2022-10-24
### Fixed
- More fixes for reporter-bugzilla-python

## [2.17.4] - 2022-09-14
### Fixed
- Various fixes of reporter-bugzilla-python

## [2.17.3] - 2022-09-12
### Fixed
- Fix configure options and conditional building of optional features

## [2.17.2] - 2022-08-18
- Replace the reporter-bugzilla binary with a Python script in Rawhide

## [2.17.1] - 2022-03-10
### Changed
- reporter-bugzilla: send API key in HTTP header for Red Hat Bugzilla

## [2.17.0] - 2022-02-17
### Changed
- reporter-bugzilla: use API key instead of username+password

## [2.16.0] - 2022-01-17
### Removed
- Direct libproxy dependency

### Changed
- Bumped satyr dependency to 0.38

### Fixed
- Memory management issues
- reporter-bugzilla: fix subcomponent handling for RHEL
- Retry XML-RPC calls when uploading attachments

## [2.15.2] - 2021-06-02
### Changed
- Use GLib for computing SHA-1 digests

### Removed
- Remove `libreport_hex2bin()` and `libreport_bin2hex()` functions.

### Fixed
- Memory management issues

## [2.15.1] - 2021-05-04
### Added
- ignored_words: add more "key" variations
- Add support for excluding whole elements from search for sensitive words

### Changed
- libreport-plugin-mailx now depends on /usr/bin/mailx (also provided by s-nail) instead of the mailx package

### Fixed
- Memory management issues

## [2.15.0] - 2020-03-03
### Added
- Add support for sub-components in RH Bugzilla

### Changed
- Replace libtar with libarchive
- Overall code style and maintainability improvement

### Fixed
- Fix bad error detection in reporter-bugzilla
- Improve sensitive word list
- Fix various memory management bugs

## [2.14.0] - 2020-08-13
### Added
- forbidden_words: Add potentially sensitive env vars
- lib: Add version script for libreport

### Changed
- lib: compress: Use libarchive
- Replace various utility functions with stock GLib ones
- gtk,lib: Update symbol list

### Fixed
- dd: Update dd_get_owner to handle error return values
- dirsize: Don't pick .lock'd dirs for deletion
- setgid instead of setuid the abrt-action-install-debuginfo-to-abrt-cache
- Various coding style improvements
- Various memory management fixes
- lib: Check for errors when opening files
- gtk-helpers: Check return value
- doc: Exclude more files with --without-bugzilla
- lib: Don’t use external executables for decompression

### Removed
- lib: Decommission libreport_list_free_with_free
- Drop Red Hat Customer Portal reporter
- ureport: Drop Strata integration
- lib: Remove creates-items tag parsing in event definitions

## [2.13.1] - 2020-05-11
### Fixed
Fix broken abrt-vmcore.service due to bad namespacing

## [2.13.0] - 2020-04-23
### Added
Support new "time" and "time_for_children" kernel namespaces

### Changed
Remove preprocessor namespacing in favor of function names prefixes

### Fixed
gui-wizard-gtk: Wrap event log messages
client-python: Accomodate for multiple debug directories

### Removed
lib: Drop D-Bus code
plugins: reporter-rhtsupport: Drop unused debugging code

## [2.12.0] - 2020-02-06
### Changed
- Use Nettle for computing SHA-1 digests
- Move augeas lenses to new subdirectory

### Fixed
- Fix runtime warning

## [2.11.3] - 2019-11-14
### Changed
- Updated maintenance script for pulling localization.

### Fixed
- Infinite loop leading to a crash in report-gtk.
- Double-free condition causing a crash in report-gtk.

### Removed
- Expert mode in report-gtk.
- Option to override UI definition in report-gtk.
- Unused build scripts.

## [2.11.2] - 2019-10-23
### Fixed
- Corrected name of post_report event in Java and JavaScript reporting workflows for Fedora and RHEL
- Improved memory handling in report-gtk

## [2.11.1] - 2019-10-16
### Fixed
- Fixed a double-free condition in libreport-gtk

## [2.11.0] - 2019-10-11
### Changed
- Dropped Python 2 support
- Removed option for emergency analysis/reporting
- Removed D-Bus interface introspection files
- Reporting gracefully skips matched events which have no associated actions

### Fixed
- Correct expansion of wildcards in event chains
- Skip directories in which sosreport is being generated

## [2.10.1] - 2019-07-03
### Changed
- Clearer warnings about missing report elements.
- Export workflow when saving server response.
- uReport workflow moved to plugin-ureport subpackage.
- Refined composition of error messages during problem reporting.

### Fixed
- Fixed dump dir lock up in case of delete failure.

## [2.10.0] - 2019-02-02
### Added
- Export used workflow to reported_to

### Changed
- Improve network bandwidth usage when looking for duplicate Bugzilla reports
- Improve emergency analysis output

### Fixed
- Fix Bugzilla reporter spamming instances running version 5 and up
- Fix excluding data in reporting wizard

## [2.9.7] - 2018-12-07
### Changed
- Visual improvements in gui wizard

### Added
- Install debuginfo of all required packages as well


## [2.9.6] - 2018-10-05
### Changed
- Removed option to screencast problems when reporting to Bugzilla.
- libreport-filesystem arch changed to noarch.
- python-rhsm dependency was replaced with subscription-manager-rhsm.

### Fixed
- Fixed majority of bugs found by Coverity Scan.
- Fixed issue with quotes in configuration file for reporter-mailx.
- Fixed bug causing abrt-cli to segfault.

## [2.9.5] - 2018-04-24
### Changed
- Actualize spec file according to downstream
- Conditionalize the Python2 and Python3

### Fixed
- Fix tests if configure --without-python2

## [2.9.4] - 2018-03-27
### Added
- Added support for reporter configurations stored in user's home config
  directory.

### Changed
- Reporter-bugzilla shows which Bugzilla login is required to enter.
- Added certificate that was previously included in redhat-access-insights
  and removed requirements for this package.

### Fixed
- Fix a bug that caused that user's mail configuration for mailx reporter
  was ignored.

## [2.9.3] - 2017-11-02
### Added
- Added workflow for adding data to existing case.
- Enabled reporting of unpackaged executables.
- Allowed python to be optional at build time.

### Changed
- Python [23] binary packages were renamed to python[23]-libreport.
- Requires pythonX-dnf instead of dnf.

### Fixed
- Fix client-python's attempts to unlink None.
- Fix error in finding executable basename in rep-sys-journal.


## [2.9.2] - 2017-08-25
### Added
- Add count field into default logs, not just FULL dump. It is useful to know
if some problem occurred first time or is it problem that happens a lot.
- Add journal entry PROBLEM_DIR. This features is requested by ABRT cockpit
plugin.
- Add newly added cpuinfo files into editable files list. This files are newly
saved by ABRT because some bugs are related to HW acceleration.

### Fixed
- Fix libreport augeas for trailing whitespaces and around value separator (=)
in libreport configs.
- Update glib minimal version dependency, this bug caused update problems when
updating libreport and pre-2.28 glib was installed.
- Fix missing newline when asking for password.
- Fix a bug causing inconsistent order of username and password fields when
reporting bugs.


## [2.9.1] - 2017-03-16
### Added
- New element 'container_rootfs' has been introduced. The element should
contain file system path of container's root directory.

### Changed
- Deciding if a process has own root is no longer based on comparing inodes of
process' root and system root but mountinfo results are compared instead. This
approach does correctly recognize chroot in a container.

### Fixed
- Fix several critical bugs affecting results in parsing of
the /proc/[pid]/mountinfo file.


## [2.9.0]
### Added
- Introduce new plugin for reporting problems to systemd-journal.
- Add workflow definitions for reporting JavaScript stack traces.
- Enhance the reportclient python module with support for $releasever in the
debug info downloader to be able to download debug info packages for the
version of operating system captured in problem details.
- Introduced a new global configuration option 'stop_on_not_reportable'
controlled by the 'ABRT_STOP_ON_NOT_REPORTABLE' environment variable to enable
clients to force libreport to always report problems regardless of possible
sensitive data leaks and report usability concerns.

### Changed
- Add "systemd-logind" and "hawkey" to the list of not highlighted sensitive
words.
- Enhance the Bugzilla reporter to include only essential packaging details to
make Bugzilla bug reports easier to comprehend.
- Generate nicer truncated backtraces by filtering out less valuable functions.
- Ask users to do security review of several more files.
- Use lz4 instead of lz4cat because the latter has stopped working.
- Reword several log messages to provide users with more valuable output.
- Move the lookup for Bodhi updates from the C/C++ analysis to a separate step
in Fedora workflows.

### Fixed
- Start controlling logging verbosity through ABRT_VERBOSE in the reportclient
python module.
- Test processes for own root by checking /proc/1/root instead of / to be able
to run libreport code in a container or a changed root environment.
- Fix formatting of the configuration format in 'man reporter-mailix'.
- Refuse to parse negative number as unsigned int when parsing configuration
files.
- Correctly detect errors by resetting errno to no error before calling
functions reporting errors through errno.


[Unreleased]: https://github.com/abrt/libreport/compare/2.17.11...HEAD
[2.17.11]: https://github.com/abrt/libreport/compare/2.17.10...2.17.11
[2.17.10]: https://github.com/abrt/libreport/compare/2.17.9...2.17.10
[2.17.9]: https://github.com/abrt/libreport/compare/2.17.8...2.17.9
[2.17.8]: https://github.com/abrt/libreport/compare/2.17.7...2.17.8
[2.17.7]: https://github.com/abrt/libreport/compare/2.17.6...2.17.7
[2.17.6]: https://github.com/abrt/libreport/compare/2.17.5...2.17.6
[2.17.5]: https://github.com/abrt/libreport/compare/2.17.4...2.17.5
[2.17.4]: https://github.com/abrt/libreport/compare/2.17.3...2.17.4
[2.17.3]: https://github.com/abrt/libreport/compare/2.17.2...2.17.3
[2.17.2]: https://github.com/abrt/libreport/compare/2.17.1...2.17.2
[2.17.1]: https://github.com/abrt/libreport/compare/2.17.0...2.17.1
[2.17.0]: https://github.com/abrt/libreport/compare/2.16.0...2.17.0
[2.16.0]: https://github.com/abrt/libreport/compare/2.15.2...2.16.0
[2.15.2]: https://github.com/abrt/libreport/compare/2.15.1...2.15.2
[2.15.1]: https://github.com/abrt/libreport/compare/2.15.0...2.15.1
[2.15.0]: https://github.com/abrt/libreport/compare/2.14.0...2.15.0
[2.14.0]: https://github.com/abrt/libreport/compare/2.13.1...2.14.0
[2.13.1]: https://github.com/abrt/libreport/compare/2.13.0...2.13.1
[2.13.0]: https://github.com/abrt/libreport/compare/2.12.0...2.13.0
[2.12.0]: https://github.com/abrt/libreport/compare/2.11.3...2.12.0
[2.11.3]: https://github.com/abrt/libreport/compare/2.11.2...2.11.3
[2.11.2]: https://github.com/abrt/libreport/compare/2.11.1...2.11.2
[2.11.1]: https://github.com/abrt/libreport/compare/2.11.0...2.11.1
[2.11.0]: https://github.com/abrt/libreport/compare/2.10.1...2.11.0
[2.10.1]: https://github.com/abrt/libreport/compare/2.10.0...2.10.1
[2.10.0]: https://github.com/abrt/libreport/compare/2.9.7...2.10.0
[2.9.7]: https://github.com/abrt/libreport/compare/2.9.6...2.9.7
[2.9.6]: https://github.com/abrt/libreport/compare/2.9.5...2.9.6
[2.9.5]: https://github.com/abrt/libreport/compare/2.9.4...2.9.5
[2.9.4]: https://github.com/abrt/libreport/compare/2.9.3...2.9.4
[2.9.3]: https://github.com/abrt/libreport/compare/2.9.2...2.9.3
[2.9.2]: https://github.com/abrt/libreport/compare/2.9.1...2.9.2
[2.9.1]: https://github.com/abrt/libreport/compare/2.9.0...2.9.1
[2.9.0]: https://github.com/abrt/libreport/compare/2.8.0...2.9.0
