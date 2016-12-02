# Change Log
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/)
and this project adheres to [Semantic Versioning](http://semver.org/).

## [Unreleased]

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


[Unreleased]: https://github.com/abrt/libreport/compare/2.9.0...HEAD
[2.9.0]: https://github.com/abrt/libreport/compare/2.8.0...2.9.0
