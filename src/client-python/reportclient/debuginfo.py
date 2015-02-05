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
"""
    This module provides classes and functions used to download and manage
    debuginfos.
"""

import sys
import os
import time
import errno
import shutil
from subprocess import Popen

from reportclient import (_, log1, log2, RETURN_OK, RETURN_FAILURE,
                          RETURN_CANCEL_BY_USER, verbose, ask_yes_no,
                          error_msg)

def ensure_abrt_uid(fn):
    """
    Ensures that the function is called using abrt's uid and gid

    Returns:
        Either an unchanged function object or a wrapper function object for
        the function.
    """

    import pwd
    current_uid = os.getuid()
    current_gid = os.getgid()
    abrt = pwd.getpwnam("abrt")

    # if we're are already running as abrt, don't do anything
    if abrt.pw_uid == current_uid and abrt.pw_gid == current_gid:
        return fn

    def wrapped(*args, **kwargs):
        """
        Wrapper function around the called function.

        Sets up uid and gid to match abrt's and after the function finishes
        rolls its uid and gid back.

        Returns:
            Return value of the wrapped function.
        """

        # switch to abrt
        os.setegid(abrt.pw_gid)
        os.seteuid(abrt.pw_uid)
        # extract the files as abrt:abrt
        retval = fn(*args, **kwargs)
        # switch back to whatever we were
        os.seteuid(current_uid)
        os.setegid(current_gid)
        return retval

    return wrapped

# TODO: unpack just required debuginfo and not entire rpm?
# ..that can lead to: foo.c No such file and directory
# files is not used...
@ensure_abrt_uid
def unpack_rpm(package_full_path, files, tmp_dir, destdir, exact_files=False):
    """
    Unpacks a single rpm located in tmp_dir into destdir.

    Arguments:
        package_full_path - full file system path to the rpm file
        files - files to extract from the rpm
        tmp_dir - temporary directory where the rpm file is located
        destdir - destination directory for the rpm package extraction
        exact_files - extract only specified files

    Returns:
        RETURN_FAILURE in case of a serious problem
    """

    log1("Extracting %s to %s", package_full_path, destdir)
    log2("%s", files)
    print(_("Extracting cpio from {0}").format(package_full_path))
    unpacked_cpio_path = tmp_dir + "/unpacked.cpio"
    try:
        unpacked_cpio = open(unpacked_cpio_path, 'wb')
    except IOError as ex:
        print(_("Can't write to '{0}': {1}").format(unpacked_cpio_path, ex))
        return RETURN_FAILURE

    rpm2cpio = Popen(["rpm2cpio", package_full_path],
                       stdout = unpacked_cpio, bufsize = -1)
    retcode = rpm2cpio.wait()

    if retcode == 0:
        log1("cpio written OK")
    else:
        unpacked_cpio.close()
        print(_("Can't extract package '{0}'").format(package_full_path))
        return RETURN_FAILURE

    # close the file
    unpacked_cpio.close()
    # and open it for reading
    unpacked_cpio = open(unpacked_cpio_path, 'rb')

    print(_("Caching files from {0} made from {1}").format("unpacked.cpio", os.path.basename(package_full_path)))

    file_patterns = ""
    cpio_args = ["cpio", "-idu"]
    if exact_files:
        for filename in files:
            file_patterns += "." + filename + " "
        cpio_args = ["cpio", "-idu", file_patterns.strip()]

    with open("/dev/null", "w") as null:
        cpio = Popen(cpio_args, cwd=destdir, bufsize=-1,
                     stdin=unpacked_cpio, stdout=null, stderr=null)
        retcode = cpio.wait()

    if retcode == 0:
        log1("files extracted OK")
        #print _("Removing temporary cpio file")
        os.unlink(unpacked_cpio_path)
    else:
        print(_("Can't extract files from '{0}'").format(unpacked_cpio_path))
        return RETURN_FAILURE

def clean_up(tmp_dir):
    """
    Removes the temporary directory.
    """

    if tmp_dir:
        try:
            shutil.rmtree(tmp_dir)
        except OSError as ex:
            if ex.errno != errno.ENOENT:
                error_msg(_("Can't remove '{0}': {1}").format(tmp_dir, str(ex)))

class DownloadProgress(object):
    """
    This class serves as a download progress handler.
    """

    def __init__(self, total_pkgs):
        """
        Sets up instance variables

        Arguments:
            total_pkgs - number of packages to download
        """

        self.total_pkgs = total_pkgs
        self.downloaded_pkgs = 0
        self.last_pct = 0
        self.last_time = 0

    def update(self, name, pct):
        """
        A method used to update the progress

        Arguments:
            name - filename
            pct  - percent downloaded
        """

        if pct == self.last_pct:
            log2("percentage is the same, not updating progress")
            return

        self.last_pct = pct
        # if run from terminal we can have fancy output
        if sys.stdout.isatty():
            sys.stdout.write("\033[sDownloading (%i of %i) %s: %3u%%\033[u"
                    % (self.downloaded_pkgs + 1, self.total_pkgs, name, pct)
            )
            if pct == 100:
                #print (_("Downloading (%i of %i) %s: %3u%%")
                #        % (self.downloaded_pkgs + 1, self.total_pkgs, name, pct)
                #)
                print(_("Downloading ({0} of {1}) {2}: {3:3}%").format(
                        self.downloaded_pkgs + 1, self.total_pkgs, name, pct
                        )
                )
        # but we want machine friendly output when spawned from abrt-server
        else:
            t = time.time()
            if self.last_time == 0:
                self.last_time = t
            # update only every 5 seconds
            if pct == 100 or self.last_time > t or t - self.last_time >= 5:
                print(_("Downloading ({0} of {1}) {2}: {3:3}%").format(
                        self.downloaded_pkgs + 1, self.total_pkgs, name, pct
                        )
                )
                self.last_time = t
                if pct == 100:
                    self.last_time = 0

        sys.stdout.flush()


class DebugInfoDownload(object):
    """
    This class is used to manage download of debuginfos.
    """

    def __init__(self, cache, tmp, repo_pattern="*debug*", keep_rpms=False,
                 noninteractive=True):
        self.old_stdout = -1
        self.cachedir = cache
        self.tmpdir = tmp
        self.keeprpms = keep_rpms
        self.noninteractive = noninteractive
        self.repo_pattern = repo_pattern

    def mute_stdout(self):
        """
        Links sys.stdout with /dev/null and saves the old stdout
        """

        if verbose < 2:
            self.old_stdout = sys.stdout
            sys.stdout = open("/dev/null", "w")

    def unmute_stdout(self):
        """
        Replaces sys.stdout by stdout saved using mute
        """

        if verbose < 2:
            if self.old_stdout != -1:
                sys.stdout = self.old_stdout
            else:
                print("ERR: unmute called without mute?")

    @ensure_abrt_uid
    def setup_tmp_dirs(self):
        if not os.path.exists(self.tmpdir):
            try:
                os.makedirs(self.tmpdir)
            except OSError as ex:
                print("Can't create tmpdir: %s" % ex)
                return RETURN_FAILURE
        if not os.path.exists(self.cachedir):
            try:
                os.makedirs(self.cachedir)
            except OSError as ex:
                print("Can't create cachedir: %s" % ex)
                return RETURN_FAILURE

        return RETURN_OK

    def prepare(self):
        pass

    def initialize_progress(self, updater):
        pass

    def initialize_repositories(self):
        pass

    def triage(self, files):
        pass

    def download_package(self, pkg):
        pass

    # return value will be used as exitcode. So 0 = ok, !0 - error
    def download(self, files, download_exact_files=False):
        """
        Downloads rpms shipping given files into a temporary directory

        Arguments:
            file - a list of files to download
            download_exact_files - extract only specified files

        Returns:
            RETURN_OK if all goes well.
            RETURN_FAILURE in case it cannot set up either of the directories.
        """

        # nothing to download?
        if not files:
            return RETURN_FAILURE

        # set up tmp and cache dirs so that we can check free space in both
        retval = self.setup_tmp_dirs()
        if retval != RETURN_OK:
            return retval

        print(_("Initializing package manager"))
        self.prepare()
        #if verbose == 0:
        #    # this suppress yum messages about setting up repositories
        #    mute_stdout()

        # This takes some time, let user know what we are doing
        print(_("Setting up repositories"))
        self.initialize_repositories()

        #if verbose == 0:
        #    # re-enable the output to stdout
        #    unmute_stdout()

        print(_("Looking for needed packages in repositories"))
        package_files_dict, not_found, todownload_size, installed_size = self.triage(files)

        if verbose != 0 or len(not_found) != 0:
            print(_("Can't find packages for {0} debuginfo files").format(len(not_found)))
        if verbose != 0 or len(package_files_dict) != 0:
            print(_("Packages to download: {0}").format(len(package_files_dict)))
            question = _("Downloading {0:.2f}Mb, installed size: {1:.2f}Mb. Continue?").format(
                         todownload_size / (1024*1024),
                         installed_size / (1024*1024)
                        )
            if self.noninteractive == False and not ask_yes_no(question):
                print(_("Download cancelled by user"))
                return RETURN_CANCEL_BY_USER
            # check if there is enough free space in both tmp and cache
            res = os.statvfs(self.tmpdir)
            tmp_space = float(res.f_bsize * res.f_bavail) / (1024*1024)
            if (todownload_size / (1024*1024)) > tmp_space:
                question = _("Warning: Not enough free space in tmp dir '{0}'"
                             " ({1:.2f}Mb left). Continue?").format(
                    self.tmpdir, tmp_space)
                if not self.noninteractive and not ask_yes_no(question):
                    print(_("Download cancelled by user"))
                    return RETURN_CANCEL_BY_USER
            res = os.statvfs(self.cachedir)
            cache_space = float(res.f_bsize * res.f_bavail) / (1024*1024)
            if (installed_size / (1024*1024)) > cache_space:
                question = _("Warning: Not enough free space in cache dir "
                             "'{0}' ({1:.2f}Mb left). Continue?").format(
                    self.cachedir, cache_space)
                if not self.noninteractive and not ask_yes_no(question):
                    print(_("Download cancelled by user"))
                    return RETURN_CANCEL_BY_USER

        progress_observer = DownloadProgress(len(package_files_dict))
        self.initialize_progress(progress_observer)

        for pkg, files in package_files_dict.items():
            # Download
            package_full_path, err = self.download_package(pkg)

            if err:
                # I observed a zero-length file left on error,
                # which prevents cleanup later. Fix it:
                try:
                    os.unlink(package_full_path)
                except OSError:
                    pass
                print(_("Downloading package {0} failed").format(pkg))
            else:
                unpack_result = unpack_rpm(package_full_path, files, self.tmpdir,
                                           self.cachedir, exact_files=download_exact_files)

                if unpack_result == RETURN_FAILURE:
                    # recursively delete the temp dir on failure
                    print(_("Unpacking failed, aborting download..."))
                    clean_up(self.tmpdir)
                    return RETURN_FAILURE

                if not self.keeprpms:
                    log1("keeprpms = False, removing %s", package_full_path)
                    os.unlink(package_full_path)

            progress_observer.downloaded_pkgs += 1

        if not self.keeprpms and os.path.exists(self.tmpdir):
            # Was: "All downloaded packages have been extracted, removing..."
            # but it was appearing even if no packages were in fact extracted
            # (say, when there was one package, and it has download error).
            print(_("Removing {0}").format(self.tmpdir))
            try:
                os.rmdir(self.tmpdir)
            except OSError:
                error_msg(_("Can't remove {0}, probably contains an error log").format(self.tmpdir))

        return RETURN_OK

def build_ids_to_path(pfx, build_ids):
    """
    Transforms build ids into a path.

    build_id1=${build_id:0:2}
    build_id2=${build_id:2}
    file="usr/lib/debug/.build-id/$build_id1/$build_id2.debug"
    """

    return ["%s/usr/lib/debug/.build-id/%s/%s.debug" % (pfx, b_id[:2], b_id[2:]) for b_id in build_ids]

# beware this finds only missing libraries, but not the executable itself ..

def filter_installed_debuginfos(build_ids, cache_dirs):
    """
    Checks for installed debuginfos.

    Arguments:
        build_ids - string containing build ids
        cache_dirs - list of cache directories

    Returns:
        List of missing debuginfo files.
    """

    files = build_ids_to_path("", build_ids)
    missing = []

    # 1st pass -> search in /usr/lib
    for debuginfo_path in files:
        log2("looking: %s", debuginfo_path)
        if os.path.exists(debuginfo_path):
            log2("found: %s", debuginfo_path)
            continue
        log2("not found: %s", debuginfo_path)
        missing.append(debuginfo_path)

    if missing:
        files = missing
        missing = []
    else: # nothing is missing, we can stop looking
        return missing

    for cache_dir in cache_dirs:
        log2("looking in %s" % cache_dir)
        for debuginfo_path in files:
            cache_debuginfo_path = cache_dir + debuginfo_path
            log2("looking: %s", cache_debuginfo_path)
            if os.path.exists(cache_debuginfo_path):
                log2("found: %s", cache_debuginfo_path)
                continue
            log2("not found: %s", debuginfo_path)
            missing.append(debuginfo_path)
        # in next iteration look only for files missing
        # from previous iterations
        if missing:
            files = missing
            missing = []
        else: # nothing is missing, we can stop looking
            return missing

    return files
