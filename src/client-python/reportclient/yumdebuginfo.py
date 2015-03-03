#!/usr/bin/python
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

from yum import _, YumBase
from yum.callbacks import DownloadBaseCallback
from yum.Errors import YumBaseError

from reportclient.debuginfo import DebugInfoDownload
from reportclient import (_, log1, log2)

class YumDownloadCallback(DownloadBaseCallback):
    """
    This class serves as a download progress handler for yum's progress bar.
    """

    def __init__(self, observer):
        """
        Sets up instance variables

        Arguments:
            total_pkgs - number of packages to download
        """

        DownloadBaseCallback.__init__(self)

        self.observer = observer

    def updateProgress(self, name, frac, fread, ftime):
        """
        A method used to update the progress

        Arguments:
            name - filename
            frac - progress fracment (0 -> 1)
            fread - formated string containing BytesRead
            ftime - formated string containing remaining or elapsed time
        """

        self.observer.update(name, int(frac * 100))

def downloadErrorCallback(callBackObj):
    """
    A callback function for mirror errors.
    """

    print (_("Problem '{0!s}' occured while downloading from mirror: '{1!s}'. Trying next one").format(
        str(callBackObj.exception), callBackObj.mirror))
    # explanation of the return value can be found here:
    # /usr/lib/python2.7/site-packages/urlgrabber/mirror.py
    return {'fail':0}


class YumDebugInfoDownload(DebugInfoDownload):

    def __init__(self, cache, tmp, repo_pattern="*debug*", keep_rpms=False,
                 noninteractive=True):
        super(YumDebugInfoDownload, self).__init__(cache, tmp, repo_pattern, keep_rpms, noninteractive)

        self.base = YumBase()

    def initialize_progress(self, updater):
        self.progress = YumDownloadCallback(updater)
        self.base.repos.setProgressBar(self.progress)
        self.base.repos.setMirrorFailureCallback(downloadErrorCallback)

    def prepare(self):
        self.mute_stdout()
        #self.conf.cache = os.geteuid() != 0
        # Setup yum (Ts, RPM db, Repo & Sack)
        # doConfigSetup() takes some time, let user know what we are doing
        try:
            # Saw this exception here:
            # cannot open Packages index using db3 - Permission denied (13)
            # yum.Errors.YumBaseError: Error: rpmdb open failed
            self.base.doConfigSetup()
        except YumBaseError as ex:
            self.unmute_stdout()
            print(_("Error initializing yum (YumBase.doConfigSetup): '{0!s}'").format(str(ex)))
            #return 1 - can't do this in constructor
            exit(1)
        self.unmute_stdout()

        # make yumdownloader work as non root user
        if not self.base.setCacheDir():
            print(_("Error: can't make cachedir, exiting"))
            return RETURN_FAILURE

        # disable all not needed
        for repo in self.base.repos.listEnabled():
            try:
                repo.close()
                self.base.repos.disableRepo(repo.id)
            except YumBaseError as ex:
                print(_("Can't disable repository '{0!s}': {1!s}").format(repo.id, str(ex)))

    def initialize_repositories(self):
        # setting-up repos one-by-one, so we can skip the broken ones...
        # this helps when users are using 3rd party repos like rpmfusion
        # in rawhide it results in: Can't find valid base url...
        for r in self.base.repos.findRepos(pattern=self.repo_pattern):
            try:
                rid = self.base.repos.enableRepo(r.id)
                self.base.repos.doSetup(thisrepo=str(r.id))
                log1("enabled repo %s", rid)
                setattr(r, "skip_if_unavailable", True)
                # yes, we want async download, otherwise our progressCallback
                # is not called and the internal yum's one  is used,
                # which causes artifacts on output
                try:
                    setattr(r, "_async", False)
                except (NameError, AttributeError) as ex:
                    print(str(ex))
                    print(_("Can't disable async download, the output might contain artifacts!"))
            except YumBaseError as ex:
                print(_("Can't setup {0}: {1}, disabling").format(r.id, str(ex)))
                self.base.repos.disableRepo(r.id)

        # This is somewhat "magic", it unpacks the metadata making it usable.
        # Looks like this is the moment when yum talks to remote servers,
        # which takes time (sometimes minutes), let user know why
        # we have "paused":
        try:
            self.base.repos.populateSack(mdtype='metadata', cacheonly=1)
        except YumBaseError as ex:
            print(_("Error retrieving metadata: '{0!s}'").format(str(ex)))
            #we don't want to die here, some metadata might be already retrieved
            # so there is a chance we already have what we need
            #return 1

        try:
            # Saw this exception here:
            # raise Errors.NoMoreMirrorsRepoError, errstr
            # NoMoreMirrorsRepoError: failure:
            # repodata/7e6632b82c91a2e88a66ad848e231f14c48259cbf3a1c3e992a77b1fc0e9d2f6-filelists.sqlite.bz2
            # from fedora-debuginfo: [Errno 256] No more mirrors to try.
            self.base.repos.populateSack(mdtype='filelists', cacheonly=1)
        except YumBaseError as ex:
            print(_("Error retrieving filelists: '{0!s}'").format(str(ex)))
            # we don't want to die here, some repos might be already processed
            # so there is a chance we already have what we need
            #return 1

    def triage(self, files):
        not_found = []
        package_files_dict = {}
        todownload_size = 0
        installed_size = 0
        for debuginfo_path in files:
            log2("yum whatprovides %s", debuginfo_path)
            pkg = self.base.pkgSack.searchFiles(debuginfo_path)
            # sometimes one file is provided by more rpms, we can use either of
            # them, so let's use the first match
            if pkg:
                if pkg[0] in package_files_dict.keys():
                    package_files_dict[pkg[0]].append(debuginfo_path)
                else:
                    package_files_dict[pkg[0]] = [debuginfo_path]
                    todownload_size += float(pkg[0].size)
                    installed_size += float(pkg[0].installedsize)

                log2("found pkg for %s: %s", debuginfo_path, pkg[0])
            else:
                log2("not found pkg for %s", debuginfo_path)
                not_found.append(debuginfo_path)

        return (package_files_dict, not_found, todownload_size, installed_size)

    def download_package(self, pkg):
        remote = pkg.returnSimple('relativepath')
        local = os.path.basename(remote)
        local = os.path.join(self.tmpdir, local)

        remote_path = pkg.returnSimple('remote_url')
        # check if the pkg is in a local repo and copy it if it is
        err = None
        if remote_path.startswith('file:///'):
            pkg_path = remote_path[7:]
            log2("copying from local repo: %s", remote)
            try:
                shutil.copy(pkg_path, local)
            except OSError as ex:
                err = _("Cannot copy file '{0}': {1}").format(pkg_path, str(ex))
        else:
            # pkg is in a remote repo, we need to download it to tmpdir
            pkg.localpath = local # Hack: to set the localpath we want
            err = self.base.downloadPkgs(pkglist=[pkg])

        # normalize the name
        # just str(pkg) doesn't work because it can have epoch
        return (local, err)
