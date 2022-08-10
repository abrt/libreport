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
from typing import Dict, List, Union

import reportclient.internal.const as const
from reportclient.internal.configuration_files import ConfFileLoader
from reportclient.internal.utils import string_to_bool

s_recognized_options = [const.OPT_NAME_SCRUBBED_VARIABLES,
                        const.OPT_NAME_EXCLUDED_ELEMENTS]


class GlobalConfFileLoader:
    def __init__(self, logger):
        self.logger = logger
        self.conf_file_loader = ConfFileLoader(logger)
        self.s_global_settings: Dict[str, Union[str, int]] = {}

    def libreport_get_global_always_excluded_elements(self):
        env_exclude = os.environ.get('EXCLUDE_FROM_REPORT')
        gc_exclude = self.s_global_settings.get(const.OPT_NAME_EXCLUDED_ELEMENTS)

        if env_exclude and not gc_exclude:
            return env_exclude.split(', ')

        if not env_exclude and gc_exclude:
            return gc_exclude.split(', ')

        if not env_exclude and not gc_exclude:
            return ['']

        return f'{env_exclude}, {gc_exclude}'.split(', ')

    def libreport_get_global_create_private_ticket(self):
        create_private = os.environ.get('ABRT_CREATE_PRIVATE_TICKET')
        return bool(create_private and string_to_bool(str(create_private).lower()))

    def libreport_set_global_create_private_ticket(self, enabled: bool):
        if enabled:
            os.environ['ABRT_CREATE_PRIVATE_TICKET'] = '1'
        else:
            os.environ.pop('ABRT_CREATE_PRIVATE_TICKET')

    def libreport_get_global_stop_on_not_reportable(self):
        stop = os.environ.get('ABRT_STOP_ON_NOT_REPORTABLE')
        return bool(stop and string_to_bool(str(stop).lower()))

    def libreport_set_global_stop_on_not_reportable(self, enabled: bool):
        if enabled:
            os.environ['ABRT_STOP_ON_NOT_REPORTABLE'] = '1'
        else:
            os.environ.pop('ABRT_STOP_ON_NOT_REPORTABLE')

    def libreport_load_global_configuration_from_dirs(self, dirs: List, dir_flags: List):
        if not self.s_global_settings:
            ret = self.conf_file_loader.libreport_load_conf_file_from_dirs_ext(
                'libreport.conf', dirs, dir_flags, self.s_global_settings, False
            )
            if not ret:
                self.logger.error('Failed to load libreport global configuration')
                return False

            for key in self.s_global_settings:
                if key not in s_recognized_options:
                    self.logger.error("libreport global configuration contains unrecognized option : '{key}'")
                    return False
        else:
            self.logger.info("NOTICE: libreport global configuration already loaded")

        return True

    def get_user_conf_base_dir(self):
        base_dir = None

        debug_base_dir = os.environ.get('LIBREPORT_DEBUG_USER_CONF_BASE_DIR')
        if debug_base_dir:
            return debug_base_dir

        user_config_dir = os.path.expanduser('~')+'/.config/'
        base_dir = os.path.join(user_config_dir, 'abrt/settings/')

        return base_dir

    def libreport_load_global_configuration(self):
        dirs = [const.CONF_DIR, self.get_user_conf_base_dir()]
        dir_flags = [const.CONF_DIR_FLAG_NONE, const.CONF_DIR_FLAG_OPTIONAL]

        return self.libreport_load_global_configuration_from_dirs(dirs, dir_flags)
