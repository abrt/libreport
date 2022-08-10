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

import os
import sys

import reportclient.internal.const as const
from reportclient.internal.dump_dir import DumpDir

editable_files = [
    const.FILENAME_COMMENT,
    const.FILENAME_BACKTRACE,
    const.FILENAME_REASON,
    # FILENAME_UID,
    # FILENAME_TIME,
    # FILENAME_ANALYZER,
    # FILENAME_EXECUTABLE,
    # FILENAME_BINARY,
    const.FILENAME_OPEN_FDS,  # user might want to hide sensitive file names
    const.FILENAME_MOUNTINFO,  # user might want to hide sensitive file paths
    # FILENAME_LIMITS,
    const.FILENAME_CMDLINE,
    const.FILENAME_CONTAINER_CMDLINE,
    # FILENAME_CGROUP,
    # FILENAME_COREDUMP,
    const.FILENAME_BACKTRACE,
    const.FILENAME_MAPS,
    const.FILENAME_SMAPS,
    const.FILENAME_ENVIRON,
    # FILENAME_DUPHASH,
    # FILENAME_CRASH_FUNCTION,
    # FILENAME_ARCHITECTURE,
    # FILENAME_KERNEL,
    # FILENAME_OS_RELEASE,
    # FILENAME_PACKAGE,
    # FILENAME_COMPONENT,
    # FILENAME_RATING,
    const.FILENAME_HOSTNAME,
    const.FILENAME_REMOTE,
    # FILENAME_TAINTED,
    # FILENAME_TAINTED_SHORT,
    # FILENAME_TAINTED_LONG,
    # FILENAME_UUID,
    # FILENAME_COUNT,
    # FILENAME_REPORTED_TO,
    # FILENAME_EVENT_LOG,
    const.FILENAME_KICKSTART_CFG,
    const.FILENAME_ANACONDA_TB,
    const.FILENAME_CPUINFO  # user might want to hide hw details not available to public
]

always_text_files = [
    const.FILENAME_CMDLINE,
    const.FILENAME_BACKTRACE,
    const.FILENAME_OS_RELEASE,
]


class ProblemDataLoader:
    def __init__(self, logger, global_config):
        self.logger = logger
        self.global_config = global_config
        self.dump_dir = DumpDir(logger=logger)

    def is_text_file_at(self, name, test_size):
        is_text = False
        is_big = False
        ratio = 10
        bad_chars = 1  # Prevents division by zero later
        txt_len = 0
        try:
            with open(name, 'r', encoding='utf-8') as file:
                content = ""
                if os.path.basename(name) in always_text_files:
                    is_text = True
                try:
                    content = file.read()
                    txt_len = len(content)
                    if txt_len > const.CD_MAX_TEXT_SIZE:
                        if is_text:
                            return (content,
                                    const.CD_FLAG_BIN | const.CD_FLAG_BIGTXT,
                                    txt_len,
                                    0)
                        is_big = True
                    for char in content[0:test_size]:
                        if not char.isprintable() and not char.isspace():
                            bad_chars += 1
                except UnicodeDecodeError:
                    return (content, const.CD_FLAG_BIN, txt_len, 0)
        except (FileNotFoundError, PermissionError) as exc:
            return (None, None, None, str(exc))

        # Add 10 to guarantee that even very short files get recognized as text
        if (txt_len + 10 / bad_chars) >= ratio:
            if is_big:
                return (content,
                        const.CD_FLAG_BIN | const.CD_FLAG_BIGTXT,
                        txt_len,
                        0)
            return (content, const.CD_FLAG_TXT, txt_len, 0)
        return (content, const.CD_FLAG_BIN, txt_len, 0)

    def problem_data_load_dump_dir_element(self, filename):
        (content, flags, size, error) = self.is_text_file_at(filename,
                                                             const.IS_TEXT_FILE_AT_PROBE_SIZE)

        if error:
            self.logger.error("is_text_file_at() returned error: %s", error)
            return (content, flags, size, error)

        if flags in [const.CD_FLAG_BIN, const.CD_FLAG_BIN | const.CD_FLAG_BIGTXT]:
            size = const.PROBLEM_ITEM_UNINITIALIZED_SIZE
            return (content, flags, size, error)

        if flags != const.CD_FLAG_TXT:
            self.logger.error("Unrecognized dump dir element flags: '{filename}' has flags '{flags}'")
            sys.exit(1)

        content = content.rstrip('\n')
        size = len(content)

        # TODO: Sanitize possibly corrupted utf8 ?

        return (content, flags, size, error)

    def problem_data_load_from_dump_dir(self, dd, exclude_items):
        problem_data = []
        try:
            with os.scandir(dd['dd_dirname']) as iterator:
                for entry in iterator:
                    if not entry.is_file(follow_symlinks=False):
                        continue
                    if entry.name in exclude_items:
                        self.logger.info("WARNING: Excluded: '%s'", entry.name)
                        continue
                    if entry.name.startswith('#') or entry.name.endswith('~'):
                        self.logger.info("WARNING: Excluded (editor backup file): '%s'", entry.name)
                        continue
                    (content, flags, size, error) = self.problem_data_load_dump_dir_element(
                            entry.path)
                    if error:
                        self.logger.error("Failed to load element %s: error %s", entry.name, error)
                        continue

                    if flags & const.CD_FLAG_TXT:
                        if entry.name in editable_files:
                            flags |= const.CD_FLAG_ISEDITABLE
                        else:
                            flags |= const.CD_FLAG_ISNOTEDITABLE
                        if entry.name in [const.FILENAME_UID,
                                          const.FILENAME_PACKAGE,
                                          const.FILENAME_CMDLINE,
                                          const.FILENAME_TIME,
                                          const.FILENAME_COUNT,
                                          const.FILENAME_REASON]:
                            flags |= const.CD_FLAG_LIST
                        if entry.name == const.FILENAME_TIME:
                            flags |= const.CD_FLAG_UNIXTIME
                    else:
                        content = entry.path
                    problem_data.append({'name': entry.name, 'content': content, 'flags': flags, 'size': size})
        except (FileNotFoundError, PermissionError) as exc:
            self.logger.error(exc)
            sys.exit(1)
        return problem_data

    def create_problem_data_for_reporting(self, dump_dir_name):
        dd = self.dump_dir.dd_opendir(dump_dir_name, flags=0)
        if not dd:
            return None  # dd_opendir already emitted error msg
        exclude_items = self.global_config.libreport_get_global_always_excluded_elements()
        problem_data = self.problem_data_load_from_dump_dir(dd, exclude_items)
        self.dump_dir.dd_close(dd)
        return problem_data
