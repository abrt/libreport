#   Copyright (C) 2013  ABRT Team
#   Copyright (C) 2013  Red Hat inc.

#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.

#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.

#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import errno
from typing import Dict

from reportclient.internal.report_result import ReportResultParser


class ReportedTo:
    def __init__(self, logger):
        self.report_result_parser = ReportResultParser(logger)

    def libreport_add_reported_to_data(self, reported_to: str, new_line: str):
        if reported_to:
            lines = reported_to.split('\n')
            for line in lines:
                if line == new_line:
                    return (reported_to, False)
            if not reported_to.endswith('\n'):
                reported_to += '\n'
            reported_to += f'{new_line}\n'
        else:
            reported_to = f'{new_line}\n'

        return (reported_to, True)

    def libreport_add_reported_to_entry_data(self, reported_to: str, result: Dict):
        buf = self.report_result_parser.report_result_to_string(result)
        if not buf:
            return -errno.EINVAL

        (reported_to, altered) = self.libreport_add_reported_to_data(reported_to, buf)

        return (reported_to, altered)

    def libreport_read_entire_reported_to_data(self, reported_to: str):
        result = []
        for line in reported_to.split('\n'):
            label, _ = line.split(': ', 1)
            result.append(self.report_result_parser.report_result_parse(line, len(label)))
        return result

    def libreport_find_in_reported_to_data(self, reported_to: str, report_label: str):
        searched = {'label': report_label,
                    'label_len': len(report_label),
                    'found': None,
                    'found_label_len': 0}

        for line in reported_to.split('\n'):
            parts = line.split(': ', 1)
            if len(parts) == 2:
                label = parts[0]
                if label == searched['label']:
                    searched['found'] = line
                    searched['found_label_len'] = len(label)

        result = None
        if searched['found']:
            result = self.report_result_parser.report_result_parse(searched['found'], searched['found_label_len'])

        return result
