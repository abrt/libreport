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

from typing import Dict, List


def pd_get_item(item_name, problem_data: List[Dict]):
    for item in problem_data:
        if item['name'] == item_name:
            return item
    return None


def pd_get_item_content(item_name, problem_data: List[Dict]):
    for item in problem_data:
        if item['name'] == item_name:
            return item['content']
    return None


def os_info_get_value(key, os_info: List[str]):
    for line in os_info:
        k, v = line.split('=')
        if k == key:
            return v.strip('\t\n "')
    return ''
