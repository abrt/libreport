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

import base64
import inspect
import json
import locale
import logging
import os
import re
import sys
import urllib.parse
from typing import Any, Dict, List, Optional

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

import reportclient.internal.const as const
from reportclient.internal.problem_data import ProblemDataLoader
from reportclient.internal.problem_utils import os_info_get_value, pd_get_item, pd_get_item_content

MAX_HOPS = 5
MAX_SUMMARY_LENGTH = 255

POST_PARAMS = ['product',
               'component',
               'summary',
               'version',
               'description',
               'op_sys',
               'platform',
               'priority',
               'severity',
               'alias',
               'assigned_to',
               'cc',
               'comment_is_private',
               'groups',
               'qa_contact',
               'status',
               'resolution',
               'target_milestone',
               'override_private',
               'flags']

logging.getLogger('requests').setLevel(logging.WARNING)
logging.getLogger('urllib3').setLevel(logging.WARNING)


class BZConnection:

    def __init__(self, logger, global_config, url='', api_key='', verify=True):
        self.url = url
        self.headers = {'Accept': 'application/json',
                        'Content-Type': 'application/json'}
        self.verify = verify
        self.api_key = api_key
        self.params = {}
        self.logger = logger
        self.global_config = global_config

        if api_key:
            self.add_api_key(api_key)

    def add_api_key(self, api_key):
        """
        API Key is not needed for bug searches so we can create BZConnection
        without it and add it just in time.
        """
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        if not api_key:
            return
        self.headers.update({'Authorization': f'Bearer {api_key}'})

    def add_api_key_param(self, api_key):
        """
        Some Bugzilla instances require the API Key in the params of the HTTP
        request rather than (or along with) the headers
        """
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        self.params['Bugzilla_api_key'] = api_key

    def attachment_create(self, bug_id: int, file_name: str, minor_update: bool):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)

        pd_loader = ProblemDataLoader(self.logger, self.global_config)
        (content, type_flags, _, error) = pd_loader.is_text_file_at(file_name,
                                                                    const.IS_TEXT_FILE_AT_PROBE_SIZE)

        if error:
            self.logger.error("Could not load file '%s'", file_name)
            return None
        if type_flags & const.CD_FLAG_TXT:
            content_type = 'text/plain'
            file_content = base64.b64encode(content.encode(locale.getpreferredencoding())).decode('utf-8')
        else:
            content_type = 'aplication/octet-stream'
            file_content = base64.b64encode(content).decode('utf-8')

        data = json.dumps({'ids': [bug_id],
                           'data': file_content,
                           'file_name': file_name,
                           'content_type': content_type,
                           'summary': f'File: {file_name}',
                           'minor_update': minor_update})

        response = requests.post(os.path.join(self.url, f'rest.cgi/bug/{bug_id}/attachment'),
                                 headers=self.headers,
                                 params=self.params,
                                 data=data,
                                 verify=self.verify)
        if response.status_code not in [200, 201]:  # HTTP 201 Created
            self.logger.error("Failed to attach '%s' to bug # %s.\nServer says: %s %s",
                              file_name, bug_id, response.status_code, response.reason)
            return None

        return response

    def attachment_create_from_problem_data(self, bug_id, file_name, problem_data):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        params = self.params.copy()

        file_content = None
        pd_item = pd_get_item(file_name, problem_data)
        if not pd_item:
            self.logger.error("Could not find attachment '%s'", file_name)
            return None
        if pd_item['flags'] & const.CD_FLAG_TXT:
            content_type = 'text/plain'
            file_content = base64.b64encode(pd_item['content'].encode('utf-8')).decode('utf-8')
        else:
            content_type = 'aplication/octet-stream'
            # "content" is actually a file path in this case...
            with open(pd_item['content'], 'rb') as pd_item_content:
                file_content = base64.b64encode(pd_item_content.read()).decode('utf-8')

        data = json.dumps({'data': file_content,
                           'file_name': file_name,
                           'content_type': content_type,
                           'summary': f"File: {file_name}",
                           'minor_update': True})

        response = requests.post(os.path.join(self.url, f'rest.cgi/bug/{bug_id}/attachment'),
                                 headers=self.headers,
                                 params=params,
                                 data=data,
                                 verify=self.verify)
        if response.status_code not in [200, 201]:  # HTTP 201 Created
            self.logger.error("Failed to attach '%s' to bug # %s.\nServer says: %s %s\n%s",
                              file_name, bug_id, response.status_code, response.reason,
                              response.text)
            return None

        return response

    def bug_create(self,
                   problem_data: List[Dict[str, Any]],
                   product: str,
                   version: str,
                   summary: str,
                   comment: str,
                   private: bool,
                   group: List) -> int:
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)

        if group:
            self.logger.debug(f'# of groups {len(group)}')

        component = pd_get_item_content(const.FILENAME_COMPONENT, problem_data)
        arch = pd_get_item_content(const.FILENAME_ARCHITECTURE, problem_data)
        duphash = pd_get_item_content(const.FILENAME_DUPHASH, problem_data)
        summary = summary[:MAX_SUMMARY_LENGTH]

        os_info = pd_get_item_content(const.FILENAME_OS_INFO, problem_data).split('\n')
        whiteboard = f'abrt_hash:{duphash};'
        for field in ['VARIANT_ID']:  # Expand if needed, see rhbz.c:533
            whiteboard += f'{field}={os_info_get_value(field, os_info)};'

        data = {
            'product': product,
            'version': version,
            'component': component,
            'summary': summary,
            'description': comment,
            'status_whiteboard': whiteboard
        }

        sub_component = None
        if product.startswith('Red Hat Enterprise Linux'):
            default_sub_components = {'binutils': 'system-version',
                                      'Documentation': 'default',
                                      'dwz': 'system-version',
                                      'dynist': 'system-version',
                                      'elfutils': 'system-version',
                                      'gcc': 'system-version',
                                      'gdb': 'system-version',
                                      'kernel': 'Other',
                                      'kernel-rt': 'Other',
                                      'kpatch': 'kpatch-utility',
                                      'ltrace': 'system-version',
                                      'lvm2': 'Default / Unclassified',
                                      'make': 'system-version',
                                      'systemtap': 'system-version',
                                      'test': 'sub1',
                                      'valgrind': 'system-version',
                                      'virtio-win': 'distribution'}
            sub_component = default_sub_components.get(component)
        if sub_component:
            data['sub_component'] = sub_component

        if arch:
            data['platform'] = arch

        if private:
            if group:
                data['groups'] = group
            else:
                self.logger.error("A private ticket creation has been requested, but no groups were"
                                  " specified, please see"
                                  " https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets"
                                  " for more info")
                return -1

        data = json.dumps(data)
        response = requests.post(os.path.join(self.url, 'rest.cgi/bug'),
                                 headers=self.headers,
                                 params=self.params,
                                 data=data,
                                 verify=self.verify)
        if response.status_code != 200:
            self.logger.error("Failed to create bug.\nServer says: %s %s\n%s",
                              response.status_code, response.reason, response.text)
            return -1

        return response.json()['id']

    def bug_get_comments(self, bug_id: int):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        params = self.params.copy()
        params.update({'ids': [bug_id]})
        response = requests.get(os.path.join(self.url, f'rest.cgi/bug/{bug_id}/comment'),
                                headers=self.headers,
                                params=params,
                                verify=self.verify)
        if response.status_code != 200:
            self.logger.error("Failed to get bug #%i info.\n"
                              "Server says: %s %s",
                              bug_id, response.status_code, response.reason)
            return None

        comments = []
        for comment in list(response.json()['bugs'].values())[0]['comments']:
            if comment.get('text'):
                comments.append(comment['text'])

        return comments

    def bug_add_comment(self, bug_id: int, comment: str):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        params = self.params.copy()
        params.update({'id': bug_id, 'comment': comment})
        response = requests.post(os.path.join(self.url, f'rest.cgi/bug/{bug_id}/comment'),
                                 headers=self.headers,
                                 params=params,
                                 verify=self.verify)
        if response.status_code != 201:
            self.logger.error("Failed to add a comment to the bug #%i.\n"
                              "Server says: %s %s",
                              bug_id, response.status_code, response.reason)
            return None
        if response.text:
            return response.json()['id']
        self.logger.warning('Created comment with unknown comment id')
        return None

    def find_best_bt_rating_in_comments(self, comments):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        if not comments:
            return 0

        best_bt_rating = 0

        for comment in comments:
            start = comment.find(f'{const.FILENAME_RATING}: ')
            if start == -1:
                self.logger.debug('comment does not contain rating')
                continue
            comment = comment[start+len(f'{const.FILENAME_RATING}: '):]
            matchobj = re.search(r'^\d+\n', comment)
            if not matchobj:
                continue
            rating = int(matchobj.group().rstrip('\n'))
            best_bt_rating = max(best_bt_rating, rating)

        return best_bt_rating

    def bug_info(self, bug_id):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        params = self.params.copy()
        params.update({'id': bug_id})
        response = requests.get(os.path.join(self.url, 'rest.cgi/bug'),
                                headers=self.headers,
                                params=params,
                                verify=self.verify)
        if response.status_code != 200:
            self.logger.error("Failed to get bug #%i info.\n"
                              "Server says: %s %s",
                              bug_id, response.status_code, response.reason)
            return None

        bug = response.json()['bugs'][0]

        bug_info: Dict[str, Any] = {}
        bug_info['bi_id']: int = int(bug['id'])
        bug_info['bi_product']: str = bug.get('product', '')
        bug_info['bi_reporter']: str = bug.get('creator', '')
        bug_info['bi_status']: str = bug.get('status', '')
        bug_info['bi_resolution']: str = bug.get('resolution', '')
        bug_info['bi_platform']: str = bug.get('platform', '')

        if bug_info['bi_status'] == 'CLOSED' and not bug_info['bi_resolution']:
            self.logger.error("Bug %i is CLOSED, but it has no RESOLUTION", bug_info['bi_id'])
            sys.exit(1)

        if (
                bug_info['bi_status'] == 'CLOSED'
                and bug_info['bi_resolution'] == 'DUPLICATE'
                and not bug.get('dupe_of')
        ):
            self.logger.error("Bug %i is CLOSED as DUPLICATE, but it has no DUP_ID",
                              bug_info['bi_id'])
            sys.exit(1)

        bug_info['bi_dup_id']: int = bug.get('dupe_of') or -1
        bug_info['bi_cc_list']: List[str] = bug.get('cc')

        bug_info['bi_comments'] = self.bug_get_comments(bug['id'])
        bug_info['bi_best_bt_rating']: int = self.find_best_bt_rating_in_comments(bug_info['bi_comments'])

        return bug_info

    def bug_search(self, query: Dict) -> List:
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        params = self.params.copy()
        params.update(query)

        # Configure requests to retry with delays (See: rhbz#2208742)
        session = requests.Session()
        retry = Retry(connect=5, backoff_factor=0.5)
        adapter = HTTPAdapter(max_retries=retry)
        session.mount('http://', adapter)
        session.mount('https://', adapter)
        try:
            response = session.get(urllib.parse.urljoin(self.url, 'rest.cgi/bug'),
                                    headers=self.headers,
                                    params=params,
                                    verify=self.verify)
        except requests.exceptions.ConnectionError as e:
            self.logger.error("Failed to connect to Bugzilla: %s", repr(e))
            sys.exit(1)

        if response.status_code != 200:
            self.logger.error("Bug search failed.\nServer says: %s %s",
                              response.status_code, response.reason)
            return []

        bugs = response.json().get('bugs')
        if bugs == None:
            self.logger.error("Bug.search(quicksearch) return value did not contain member 'bugs'")
            sys.exit(1)
        return bugs

    def bug_update(self, bug_id, update_data):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        data = json.dumps(update_data)
        response = requests.put(os.path.join(self.url, f'rest.cgi/bug/{bug_id}'),
                                headers=self.headers,
                                params=self.params,
                                data=data,
                                verify=self.verify)
        if response.status_code != 200:
            self.logger.error("Failed to update bug #%s info.\nServer says: %s %s",
                              bug_id, response.status_code, response.reason)
            return None
        # We specified one bug id to update, so there should be only one in
        # response['bugs']
        changed = response.json()["bugs"][0]["changes"]
        self.logger.info("Changed: %s", changed)
        return changed

    def find_origin_bug_closed_duplicate(self, bug_info: Optional[Dict[str, Any]]):
        self.logger.debug('-- %s', inspect.getframeinfo(inspect.currentframe()).function)
        bi_tmp = {'bi_id': bug_info['bi_id'],
                  'bi_status': 'NEW',
                  'bi_dup_id': bug_info['bi_dup_id']}

        for i in range(0, MAX_HOPS+1):
            if i == MAX_HOPS:
                self.logger.error("Bugzilla couldn't find parent of bug %d", bug_info['bi_id'])
                sys.exit(1)

            self.logger.warning('Bug %d is a duplicate, using parent bug %d',
                                bi_tmp['bi_id'], bi_tmp['bi_dup_id'])
            bug_id = bi_tmp['bi_dup_id']

            bi_tmp = self.bug_info(bug_id)

            # found a bug which is not CLOSED as DUPLICATE
            if bi_tmp['bi_dup_id'] == -1:
                break

        return bi_tmp
