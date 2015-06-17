# coding=UTF-8

## Copyright (C) 2015 ABRT team <abrt-devel-list@redhat.com>
## Copyright (C) 2015 Red Hat, Inc.

## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.

## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.

## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335  USA
import os
import sys
import shutil

import dnf
import dnf.rpm
from dnf.exceptions import DownloadError, RepoError
from dnf.callback import (STATUS_OK,
                          STATUS_DRPM,
                          STATUS_ALREADY_EXISTS,
                          STATUS_MIRROR,
                          STATUS_FAILED)

from reportclient import (_, log1, log2)
from reportclient.debuginfo import DebugInfoDownload

class DNFProgress(dnf.callback.DownloadProgress):

    def __init__(self, observer):
        super(DNFProgress, self).__init__()

        self.observer = observer

    def end(self, payload, status, msg):
        # One may objects that this call back isn't necessary because the
        # progress() callback is called when downloading finishes, but if the
        # downloaded package is already in a local cache, DNF doesn't call
        # progress() callback at all but yet we want to inform user about that
        # progress.
        if status in [STATUS_OK, STATUS_DRPM, STATUS_ALREADY_EXISTS]:
            self.observer.update(str(payload), 100)
        elif status == STATUS_MIRROR:
            # In this case dnf (librepo) tries other mirror if available
            log1("Mirror failed: %s" % (msg or "DNF did not provide more details"))
        elif status in STATUS_FAILED:
            log2("Downloading failed: %s" % (msg or "DNF did not provide more details"))
        else:
            sys.stderr.write("Unknown DNF download status: %s\n" % (msg))

    def progress(self, payload, done):
        log2("Updated a package")
        self.observer.update(str(payload), int(100 * (done / payload.download_size)))

    def start(self, total_files, total_size):
        log2("Started downloading of a package")


class DNFDebugInfoDownload(DebugInfoDownload):

    def __init__(self, cache, tmp, repo_pattern="*debug*", keep_rpms=False,
                 noninteractive=True):
        super(DNFDebugInfoDownload, self).__init__(cache, tmp, repo_pattern, keep_rpms, noninteractive)

        self.progress = None

        self.base = dnf.Base()
        self.base.logging.presetup()

    def prepare(self):
        try:
            self.base.read_all_repos()
        except dnf.exceptions.Error as ex:
            print(_("Error reading repository configuration: '{0!s}'").format(str(ex)))

    def initialize_progress(self, updater):
        self.progress = DNFProgress(updater)

    def initialize_repositories(self):
        # enable only debug repositories
        for repo in self.base.repos.all():
            repo.disable()

        for repo in self.base.repos.get_matching(self.repo_pattern):
            repo.skip_if_unavailable = True
            repo.enable()

        try:
            self.base.fill_sack()
        except RepoError as ex:
            print(_("Error setting up repositories: '{0!s}'").format(str(ex)))

    def triage(self, files):
        q = self.base.sack.query()
        i = q.available()
        package_files_dict = {}
        not_found = []
        todownload_size = 0
        installed_size = 0
        for debuginfo_path in files:
            packages = i.filter(file=debuginfo_path);
            if not packages:
                log2("not found package for %s", debuginfo_path)
                not_found.append(debuginfo_path)
            else:
                if packages[0] in package_files_dict.keys():
                    package_files_dict[packages[0]].append(debuginfo_path)
                else:
                    package_files_dict[packages[0]] = [debuginfo_path]
                    todownload_size += float(packages[0].downloadsize)
                    installed_size += float(packages[0].installsize)

                log2("found packages for %s: %s", debuginfo_path, packages[0])
        return (package_files_dict, not_found, todownload_size, installed_size)

    def download_package(self, pkg):
        try:
            self.base.download_packages([pkg], self.progress)
        except DownloadError as ex:
            return (None, str(ex))

        return (pkg.localPkg(), None)
