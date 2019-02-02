# Change Log
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

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


[Unreleased]: https://github.com/abrt/libreport/compare/2.10.0...HEAD
[2.10.0]: https://github.com/abrt/libreport/compare/2.9.7...2.10.0
[2.9.7]: https://github.com/abrt/libreport/compare/2.9.6...2.9.7
[2.9.6]: https://github.com/abrt/libreport/compare/2.9.5...2.9.6
[2.9.5]: https://github.com/abrt/libreport/compare/2.9.4...2.9.5
[2.9.4]: https://github.com/abrt/libreport/compare/2.9.3...2.9.4
[2.9.3]: https://github.com/abrt/libreport/compare/2.9.2...2.9.3
[2.9.2]: https://github.com/abrt/libreport/compare/2.9.1...2.9.2
[2.9.1]: https://github.com/abrt/libreport/compare/2.9.0...2.9.1
[2.9.0]: https://github.com/abrt/libreport/compare/2.8.0...2.9.0
