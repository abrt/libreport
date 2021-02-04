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
import sys
from typing import Dict, List, Optional

import dnf
import dnf.rpm
from dnf.exceptions import DownloadError, RepoError
from dnf.callback import (STATUS_OK,
                          STATUS_DRPM,
                          STATUS_ALREADY_EXISTS,
                          STATUS_MIRROR,
                          STATUS_FAILED)
from hawkey import Package, Query

from reportclient import (_, log1, log2)
from reportclient.debuginfo import DebugInfoDownload, DownloadProgress


DnfStatus = Optional[int]


class DNFProgress(dnf.callback.DownloadProgress):
    def __init__(self, observer: DownloadProgress):
        super(DNFProgress, self).__init__()

        self.observer = observer

    def end(self, payload: dnf.callback.Payload, status: DnfStatus,
            msg: Optional[str]) -> None:
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
        elif status == STATUS_FAILED:
            log2("Downloading failed: %s" % (msg or "DNF did not provide more details"))
        else:
            sys.stderr.write("Unknown DNF download status: %s\n" % (msg))

    def progress(self, payload: dnf.callback.Payload, done: float) -> None:
        log2("Updated a package")
        self.observer.update(str(payload), int(100 * (done / payload.download_size)))

    def start(self, total_files: int, total_size: float,
              total_drpms: Optional[int] = 0) -> None:
        log2("Started downloading of a package")


class DNFDebugInfoDownload(DebugInfoDownload):
    def __init__(self, cache: str, tmp: str, repo_pattern: str = "*debug*",
                 keep_rpms: bool = False, noninteractive: bool = True,
                 releasever: Optional[str] = None):
        super(DNFDebugInfoDownload, self).__init__(cache, tmp, repo_pattern,
                                                   keep_rpms, noninteractive)

        self.progress: Optional[DNFProgress] = None
        self.base = dnf.Base()

        if releasever is not None:
            self.base.conf.substitutions['releasever'] = releasever

    def prepare(self) -> None:
        try:
            self.base.read_all_repos()
        except dnf.exceptions.Error as ex:
            print(_("Error reading repository configuration: '{0}'").format(str(ex)))

    def initialize_progress(self, updater: DownloadProgress) -> None:
        self.progress = DNFProgress(updater)

    def initialize_repositories(self) -> None:
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

    def triage(self, files: List[str]) -> DebugInfoDownload.TriageResult:
        dnf_query = self.base.sack.query()
        dnf_available = dnf_query.available()
        package_files_dict: Dict[str, List[str]] = {}
        not_found: List[str] = []
        todownload_size: float = 0
        installed_size: float = 0

        def required_packages(query: Query, package: Package,
                              origin: Package) -> List[Package]:
            """
            Recursive function to find all required packages of required packages of ...
              origin - should stop infinite recursion (A => B => ... => X => A)
            """
            required_pkg_list: List[Package] = []
            if package.requires:
                pkg_reqs = query.filter(provides=package.requires, arch=package.arch)
                for p in pkg_reqs:
                    if p.name != origin.name and p not in required_pkg_list:
                        required_pkg_list.append(p)
                        required_pkg_list += required_packages(query, p, origin)

            return required_pkg_list

        for debuginfo_path in files:
            di_package_list = []
            packages = dnf_available.filter(file=debuginfo_path)

            if not packages:
                log2("not found package for %s", debuginfo_path)
                not_found.append(debuginfo_path)
            else:
                di_package_list.append(packages[0])
                if packages[0].requires:
                    package_reqs = required_packages(dnf_available, packages[0],
                                                     packages[0])
                    for pkg in package_reqs:
                        if pkg in di_package_list:
                            continue
                        di_package_list.append(pkg)
                        log2("found required package {0} for {1}"
                             .format(pkg, packages[0]))

                for pkg in di_package_list:
                    if pkg in package_files_dict:
                        package_files_dict[pkg].append(debuginfo_path)
                    else:
                        package_files_dict[pkg] = [debuginfo_path]
                        todownload_size += float(pkg.downloadsize)
                        installed_size += float(pkg.installsize)

                    log2("found packages for %s: %s", debuginfo_path, pkg)
        return (package_files_dict, not_found, todownload_size, installed_size)

    def download_package(self, pkg: Package) -> DebugInfoDownload.DownloadResult:
        try:
            self.base.download_packages([pkg], self.progress)
        except DownloadError as ex:
            return (None, str(ex))

        return (pkg.localPkg(), None)
