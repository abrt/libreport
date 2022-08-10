#    Copyright (C) 2022  ABRT team
#    Copyright (C) 2022  Red Hat Inc
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License along
#    with this program; if not, write to the Free Software Foundation, Inc.,
#    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

GETTEXT_PROGNAME = 'reporter-bugzilla'
CONF_DIR = '/etc/libreport'
USER_HOME_CONFIG_PATH = '/.config/libreport'
CONF_DIR_FLAG_NONE = 0
CONF_DIR_FLAG_OPTIONAL = 1

PROBLEM_ITEM_UNINITIALIZED_SIZE = 1024 * 1024 * 1024

# Filenames in problem directory filled by a hook
FILENAME_TIME = "time"                        # mandatory
FILENAME_LAST_OCCURRENCE = "last_occurrence"  # optional
FILENAME_REASON = "reason"                    # mandatory?
FILENAME_UID = "uid"                          # mandatory?
FILENAME_ANALYZER = "analyzer"
FILENAME_TYPE = "type"
FILENAME_EXECUTABLE = "executable"
FILENAME_PID = "pid"
FILENAME_TID = "tid"
FILENAME_GLOBAL_PID = "global_pid"
FILENAME_PWD = "pwd"
FILENAME_ROOTDIR = "rootdir"
FILENAME_BINARY = "binary"
FILENAME_CMDLINE = "cmdline"
FILENAME_COREDUMP = "coredump"
FILENAME_CGROUP = "cgroup"
FILENAME_BACKTRACE = "backtrace"
FILENAME_MAPS = "maps"
FILENAME_SMAPS = "smaps"
FILENAME_PROC_PID_STATUS = "proc_pid_status"
FILENAME_ENVIRON = "environ"
FILENAME_LIMITS = "limits"
FILENAME_OPEN_FDS = "open_fds"
FILENAME_MOUNTINFO = "mountinfo"
FILENAME_NAMESPACES = "namespaces"
FILENAME_CPUINFO = "cpuinfo"
# Global problem identifier which is usually generated by some "analyze_*"
# event because it may take a lot of time to obtain strong problem
# identification
FILENAME_DUPHASH = "duphash"
# Name of the function where the application crashed
FILENAME_CRASH_FUNCTION = "crash_function"
FILENAME_ARCHITECTURE = "architecture"
FILENAME_KERNEL = "kernel"
# From /etc/os-release
# os_release filename name is alredy occupied by /etc/redhat-release (see
# below) in sake of backward compatibility /etc/os-release is stored in
# os_info file
FILENAME_OS_INFO = "os_info"
FILENAME_OS_INFO_IN_ROOTDIR = "os_info_in_rootdir"
# From /etc/system-release or /etc/redhat-release
FILENAME_OS_RELEASE = "os_release"
FILENAME_OS_RELEASE_IN_ROOTDIR = "os_release_in_rootdir"

FILENAME_PACKAGE = "package"
FILENAME_COMPONENT = "component"
FILENAME_COMMENT = "comment"
FILENAME_RATING = "backtrace_rating"
FILENAME_HOSTNAME = "hostname"
# Optional. Set to "1" by abrt-handle-upload for every unpacked dump
FILENAME_REMOTE = "remote"
FILENAME_TAINTED = "kernel_tainted"
FILENAME_TAINTED_SHORT = "kernel_tainted_short"
FILENAME_TAINTED_LONG = "kernel_tainted_long"
FILENAME_VMCORE = "vmcore"
FILENAME_KERNEL_LOG = "kernel_log"
# File created by createAlertSignature() from libreport's python module
# The file should contain a description of an alert
FILENAME_DESCRIPTION = "description"
# Local problem identifier (weaker than global identifier) designed for fast
# local for fast local duplicate identification. This file is usually provided
# by crashed application (problem creator).
FILENAME_UUID = "uuid"
FILENAME_COUNT = "count"
# Multi-line list of places problem was reported.
# Recommended line format:
# "Reporter: VAR=VAL VAR=VAL"
# Use libreport_add_reported_to(dd, "line_without_newline"): it adds line
# only if it is not already there.
FILENAME_REPORTED_TO = "reported_to"
FILENAME_EVENT_LOG = "event_log"
# If exists, should contain a full sentence (with trailing period)
# which describes why this problem should not be reported.
# Example: "Your laptop firmware 1.9a is buggy, version 1.10 contains the fix."
FILENAME_NOT_REPORTABLE = "not-reportable"
FILENAME_CORE_BACKTRACE = "core_backtrace"
FILENAME_REMOTE_RESULT = "remote_result"
FILENAME_PKG_EPOCH = "pkg_epoch"
FILENAME_PKG_NAME = "pkg_name"
FILENAME_PKG_VERSION = "pkg_version"
FILENAME_PKG_RELEASE = "pkg_release"
FILENAME_PKG_ARCH = "pkg_arch"
# RHEL packages - Red Hat, inc.
FILENAME_PKG_VENDOR = "pkg_vendor"
# RHEL keys - https://access.redhat.com/security/team/key */
FILENAME_PKG_FINGERPRINT = "pkg_fingerprint"
FILENAME_USERNAME = "username"
FILENAME_ABRT_VERSION = "abrt_version"
FILENAME_EXPLOITABLE = "exploitable"
# Reproducible element is used by functions from problem_data.h */
FILENAME_REPRODUCIBLE = "reproducible"
FILENAME_REPRODUCER = "reproducer"
# File names related to Anaconda problems
FILENAME_KICKSTART_CFG = "ks.cfg"
FILENAME_ANACONDA_TB = "anaconda-tb"
# Containers
FILENAME_CONTAINER = "container"
FILENAME_CONTAINER_ID = "container_id"
FILENAME_CONTAINER_UUID = "container_uuid"
FILENAME_CONTAINER_IMAGE = "container_image"
FILENAME_CONTAINER_CMDLINE = "container_cmdline"
# Container root file-system directory as seen from the host. */
FILENAME_CONTAINER_ROOTFS = "container_rootfs"
FILENAME_DOCKER_INSPECT = "docker_inspect"
# Type of catched exception. Optional.
FILENAME_EXCEPTION_TYPE = "exception_type"

OPT_NAME_SCRUBBED_VARIABLES = "ScrubbedENVVariables"
OPT_NAME_EXCLUDED_ELEMENTS = "AlwaysExcludedElements"

IS_TEXT_FILE_AT_PROBE_SIZE = 4 * 1024

CD_TEXT_ATT_SIZE_BZ = 4 * 1024
CD_MAX_TEXT_SIZE = 8 * 1024 * 1024
CD_FLAG_BIN = 1 << 0
CD_FLAG_TXT = 1 << 1
CD_FLAG_ISEDITABLE = 1 << 2
CD_FLAG_ISNOTEDITABLE = 1 << 3
# Show this element in "short" info (report-cli -l) */
CD_FLAG_LIST = 1 << 4
CD_FLAG_UNIXTIME = 1 << 5
# If element is HUGE text, it is not read into memory (it can OOM the machine).
# Instead, it is treated as binary (CD_FLAG_BIN), but also has CD_FLAG_BIGTXT
# bit set in flags. This allows to set proper MIME type when it gets attached
# to a bug report etc.
# /
CD_FLAG_BIGTXT = 1 << 6

EXIT_CANCEL_BY_USER = 69

RHBZ_MANDATORY_MEMB = 1 << 0
RHBZ_READ_STR = 1 << 1
RHBZ_READ_INT = 1 << 2
RHBZ_MINOR_UPDATE = 1 << 3
RHBZ_PRIVATE = 1 << 4
RHBZ_BINARY_ATTACHMENT = 1 << 5
