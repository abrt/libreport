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

import configparser
import os
from typing import Dict

import reportclient.internal.const as const


class ConfFileLoader:
    def __init__(self, logger):
        self.logger = logger

    def libreport_load_conf_file(self, path: str, settings: Dict, skip_empty_keys):
        result = False

        real_path = os.path.realpath(os.path.expandvars(path))

        try:
            with open(real_path, 'r', encoding='utf-8') as handle:
                parser = configparser.ConfigParser()
                # Keep options case sensitive
                parser.optionxform = lambda option: option
                # Add a dummy section to allow parsing as ini file
                conf_text = '[config_section]\n' + handle.read()
                parser.read_string(conf_text)
                for option in parser.options('config_section'):
                    if skip_empty_keys and not parser['config_section'][option]:
                        continue
                    settings[option] = parser['config_section'][option]
                    self.logger.info("Loaded option '%s' = '%s'",
                                     option, parser['config_section'][option])
                result = True
        except (FileNotFoundError, PermissionError):
            pass

        return result

    def libreport_load_conf_file_from_dirs_ext(self, base_name, directories, dir_flags, settings, skip_empty_keys):
        if not directories:
            self.logger.error("No configuration directory specified")
            return False

        result = True

        for (directory, dir_flag) in zip(directories, dir_flags):
            conf_file = os.path.join(directory, base_name)
            if not self.libreport_load_conf_file(conf_file, settings, skip_empty_keys):
                if (dir_flags and (dir_flag & const.CONF_DIR_FLAG_OPTIONAL)):
                    self.logger.info("NOTICE: Can't open '%s'", conf_file)
                else:
                    self.logger.error("Can't open '%s'", conf_file)
                    result = False

        return result
