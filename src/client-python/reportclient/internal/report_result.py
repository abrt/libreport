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

import gettext
import os
from typing import Dict

from reportclient.internal.iso_date_string import ISODateString

_ = gettext.gettext


def report_result_get_label(result: Dict):
    if not result:
        return None

    return result['label']


def report_result_get_url(result: Dict):
    if not result:
        return None

    return result['url']


def report_result_get_message(result: Dict):
    if not result:
        return None

    return result['message']


def report_result_get_bthash(result: Dict):
    if not result:
        return None

    return result['bthash']


def report_result_get_workflow(result: Dict):
    if not result:
        return None

    return result['workflow']


def report_result_get_timestamp(result: Dict):
    if not result:
        return None

    return result['timestamp']


def report_result_set_url(result: Dict, url: str):
    if result:
        result['url'] = url


def report_result_set_message(result: Dict, message: str):
    if result:
        result['message'] = message


def report_result_set_bthash(result: Dict, bthash: str):
    if result:
        result['bthash'] = bthash


def report_result_set_workflow(result: Dict, workflow: str):
    if result:
        result['workflow'] = workflow


def report_result_set_timestamp(result: Dict, timestamp):
    if result:
        result['timestamp'] = timestamp


def report_result_new_with_label(label: str):
    if not label:
        return None
    if ':' in label:
        return None

    result = {'label': label,
              'url': '',
              'messgae': '',
              'bthash': '',
              'workflow': '',
              'timestamp': -1}

    return result


def report_result_new_with_label_from_env(label: str):
    if not label:
        return None
    if ':' in label:
        return None

    result = {'label': label,
              'url': '',
              'messgae': '',
              'bthash': '',
              'workflow': '',
              'timestamp': -1}

    workflow = os.environ.get('LIBREPORT_WORKFLOW')

    if workflow:
        result['workflow'] = workflow

    return result


class ReportResultParser:
    def __init__(self, logger):
        self.logger = logger
        self.iso_date_string = ISODateString(logger)

    def report_result_parse(self, line: str, label_length: int):
        result = {'label': '',
                  'url': '',
                  'messgae': '',
                  'bthash': '',
                  'workflow': '',
                  'timestamp': -1}

        label, rest = line.split(':', 1)

        # label_length arg is inherited from C code
        # We don't have a good use for it now
        if len(label) != label_length:
            self.logger.warning('Parsing report result:\n%s\nIncorrect label length passed',
                                line)

        result['label'] = label

        message = rest[rest.find('MSG=') + 4:]
        result['message'] = message.rstrip('\n')

        fields = rest[:rest.find('MSG=')].split(' ')

        for field in fields:
            if field.startswith('URL='):
                _, value = field.split('=', 1)
                result['url'] = value
            elif field.startswith('BTHASH='):
                _, value = field.split('=', 1)
                result['bthash'] = value
            elif field.startswith('WORKFLOW='):
                _, value = field.split('=', 1)
                result['workflow'] = value
            elif field.startswith('TIME='):
                _, value = field.split('=', 1)
                timestamp, __ = self.iso_date_string.libreport_iso_date_string_parse(value)
                if timestamp == -1:
                    self.logger.warning(_("Ignored invalid ISO date of report result '%s'"),
                                        result['label'])
                # timestamp is initialized to -1 so assign anyway
                result['timestamp'] = timestamp
        return result

    def report_result_to_string(self, result: Dict):

        if not result or not result.get('label'):
            return None

        buf = ''

        buf += f"{result['label']}:"

        if result.get('timestamp') != -1:
            buf += f" TIME={self.iso_date_string.libreport_iso_date_string(result['timestamp'])}"

        if result.get('url'):
            buf += f" URL={result['url']}"

        if result.get('bthash'):
            buf += f" BTHASH={result['bthash']}"

        if result.get('workflow'):
            buf += f" WORKFLOW={result['workflow']}"

        # MSG must be last because the value is delimited by new line character
        if result.get('message'):
            buf += f" MSG={result['workflow']}"

        return buf
