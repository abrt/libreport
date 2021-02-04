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

from abc import ABC, abstractmethod
import errno
import os
import pwd
import shutil
from subprocess import run
import sys
import tempfile
import time
from typing import Any, Callable, Dict, List, Optional, TextIO, Tuple, TypeVar, Union

from hawkey import Package

from reportclient import (_, log1, log2, RETURN_OK, RETURN_FAILURE,
                          RETURN_CANCEL_BY_USER, verbose, ask_yes_no,
                          error_msg)


MiB = 1024 * 1024
ReturnType = TypeVar("ReturnType")


def ensure_abrt_gid(fn: Callable[..., ReturnType]) -> Callable[..., ReturnType]:
    """
    Ensures that the function is called using abrt's gid

    Returns:
        Either an unchanged function object or a wrapper function object for
        the function.
    """

    current_gid = os.getgid()
    abrt = pwd.getpwnam("abrt")

    # if we're are already running as abrt, don't do anything
    if abrt.pw_gid == current_gid:
        return fn

    def wrapped(*args, **kwargs) -> ReturnType:
        """
        Wrapper function around the called function.

        Sets up gid to match abrt's and after the function finishes
        rolls its gid back.

        Returns:
            Return value of the wrapped function.
        """

        # switch to abrt group
        os.setegid(abrt.pw_gid)
        # extract the files as abrt:abrt
        retval = fn(*args, **kwargs)
        # switch back to whatever we were
        os.setegid(current_gid)
        return retval

    return wrapped


# TODO: unpack just required debuginfo and not entire rpm?
# ..that can lead to: foo.c No such file and directory
# files is not used...
@ensure_abrt_gid
def unpack_rpm(package_full_path: str, files: List[str], tmp_dir: str, destdir: str,
               exact_files: bool = False) -> int:
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
        with open(unpacked_cpio_path, "wb") as unpacked_cpio:
            rpm2cpio = run(["rpm2cpio", package_full_path], stdout=unpacked_cpio)
            retcode = rpm2cpio.returncode

            if retcode == 0:
                log1("cpio written OK")
            else:
                print(_("Can't extract package '{0}'").format(package_full_path))
                return RETURN_FAILURE
    except IOError as ex:
        print(_("Can't open '{0}' for writing: {1}").format(unpacked_cpio_path, ex))
        return RETURN_FAILURE

    print(_("Caching files from {0} made from {1}")
          .format("unpacked.cpio", os.path.basename(package_full_path)))

    cpio_args = ["cpio", "-idu"]
    if exact_files:
        file_patterns = " ".join("." + filename for filename in files)
        cpio_args.append(file_patterns)

    with open(unpacked_cpio_path, "rb") as unpacked_cpio:
        with tempfile.NamedTemporaryFile(prefix="abrt-unpacking-", dir="/tmp",
                                         delete=False) as log_file:
            log_file_name = log_file.name
            cpio = run(cpio_args, cwd=destdir, bufsize=-1,
                       stdin=unpacked_cpio, stdout=log_file, stderr=log_file)
            retcode = cpio.returncode

    if retcode == 0:
        log1("files extracted OK")
        os.unlink(log_file_name)
        os.unlink(unpacked_cpio_path)
    else:
        print(_("Can't extract files from '{0}'. For more information see '{1}'")
              .format(unpacked_cpio_path, log_file_name))
        return RETURN_FAILURE

    return RETURN_OK


def clean_up(tmp_dir: str, silent: bool = False) -> None:
    """
    Removes the temporary directory.
    """

    if tmp_dir:
        try:
            shutil.rmtree(tmp_dir)
        except OSError as ex:
            if ex.errno != errno.ENOENT and not silent:
                error_msg(_("Can't remove '{0}': {1}").format(tmp_dir, str(ex)))


class DownloadProgress:
    """
    This class serves as a download progress handler.
    """

    def __init__(self, total_pkgs: int):
        """
        Sets up instance variables

        Arguments:
            total_pkgs - number of packages to download
        """

        self.total_pkgs = total_pkgs
        self.downloaded_pkgs: int = 0
        self.last_pct: int = 0
        self.last_time: float = 0

    def update(self, name: str, pct: int) -> None:
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
            message = (_("Downloading ({0} of {1}) {2}: {3:3}%")
                       .format(self.downloaded_pkgs + 1, self.total_pkgs, name, pct))
            print("\033[s%s\033[u" % message, end='', flush=True)
            if pct == 100:
                print()
        # but we want machine friendly output when spawned from abrt-server
        else:
            t = time.time()
            if not self.last_time:
                self.last_time = t
            # update only every 5 seconds
            if pct == 100 or t - self.last_time >= 5:
                print(_("Downloading ({0} of {1}) {2}: {3:3}%")
                      .format(self.downloaded_pkgs + 1, self.total_pkgs, name, pct),
                      flush=True)
                self.last_time = t
                if pct == 100:
                    self.last_time = 0


class DebugInfoDownload(ABC):
    """
    This class is used to manage download of debuginfos.
    """

    DownloadResult = Union[Tuple[None, str], Tuple[str, None]]
    TriageResult = Tuple[Dict[str, List[str]], List[str], float, float]

    def __init__(self, cache: str, tmp: str, repo_pattern: str = "*debug*",
                 keep_rpms: bool = False, noninteractive: bool = True):
        self.old_stdout: Optional[TextIO] = None
        self.cachedir = cache
        self.tmpdir = tmp
        self.keeprpms = keep_rpms
        self.noninteractive = noninteractive
        self.repo_pattern = repo_pattern
        self.package_files_dict: Dict[str, List[str]] = {}
        self.not_found: List[str] = []
        self.todownload_size: float = 0
        self.installed_size: float = 0
        self.find_packages_run = False

    def get_download_size(self) -> float:
        return self.todownload_size

    def get_install_size(self) -> float:
        return self.installed_size

    def get_package_count(self) -> int:
        return len(self.package_files_dict)

    def mute_stdout(self) -> None:
        """
        Links sys.stdout with /dev/null and saves the old stdout
        """

        if verbose < 2:
            self.old_stdout = sys.stdout
            sys.stdout = open("/dev/null", "w")

    def unmute_stdout(self) -> None:
        """
        Replaces sys.stdout by stdout saved using mute
        """

        if verbose < 2:
            if self.old_stdout is not None:
                sys.stdout = self.old_stdout
            else:
                print("ERR: unmute called without mute?")

    @ensure_abrt_gid
    def setup_tmp_dirs(self) -> int:
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

    @abstractmethod
    def prepare(self) -> None:
        pass

    @abstractmethod
    def initialize_progress(self, updater: DownloadProgress) -> None:
        pass

    @abstractmethod
    def initialize_repositories(self) -> None:
        pass

    @abstractmethod
    def triage(self, files: List[str]) -> TriageResult:
        pass

    @abstractmethod
    def download_package(self, pkg: Package) -> DownloadResult:
        pass

    def find_packages(self, files: List[str]) -> int:
        self.find_packages_run = True
        # nothing to download?
        if not files:
            return RETURN_FAILURE

        print(_("Initializing package manager"))
        self.prepare()

        # This takes some time, let user know what we are doing
        print(_("Setting up repositories"))
        self.initialize_repositories()

        print(_("Looking for needed packages in repositories"))
        (self.package_files_dict,
         self.not_found,
         self.todownload_size,
         self.installed_size) = self.triage(files)

        return RETURN_OK

    # return value will be used as exitcode. So 0 = ok, !0 - error
    def download(self, files: List[str], download_exact_files: bool = False) -> int:
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

        if not self.find_packages_run:
            self.find_packages(files)

        if verbose or self.not_found:
            print(_("Can't find packages for {0} debuginfo files")
                  .format(len(self.not_found)))

        if verbose or self.package_files_dict:
            print(_("Packages to download: {0}")
                  .format(len(self.package_files_dict)))
            question = _(
                "Downloading {0:.2f} MiB, installed size: {1:.2f} MiB. Continue?") \
                .format(self.todownload_size / MiB,
                        self.installed_size / MiB)

            if not self.noninteractive and not ask_yes_no(question):
                print(_("Download cancelled by user"))
                return RETURN_CANCEL_BY_USER

            # check if there is enough free space in both tmp and cache
            res = os.statvfs(self.tmpdir)
            tmp_space = float(res.f_bsize * res.f_bavail) / MiB
            if (self.todownload_size / MiB) > tmp_space:
                question = _("Warning: Not enough free space in tmp dir '{0}'"
                             " ({1:.2f} MiB left). Continue?").format(
                                 self.tmpdir, tmp_space)

                if not self.noninteractive and not ask_yes_no(question):
                    print(_("Download cancelled by user"))
                    return RETURN_CANCEL_BY_USER

            res = os.statvfs(self.cachedir)
            cache_space = float(res.f_bsize * res.f_bavail) / MiB
            if (self.installed_size / MiB) > cache_space:
                question = _("Warning: Not enough free space in cache dir "
                             "'{0}' ({1:.2f} MiB left). Continue?").format(
                                 self.cachedir, cache_space)

                if not self.noninteractive and not ask_yes_no(question):
                    print(_("Download cancelled by user"))
                    return RETURN_CANCEL_BY_USER

        progress_observer = DownloadProgress(len(self.package_files_dict))
        self.initialize_progress(progress_observer)

        for pkg, files in self.package_files_dict.items():
            # Download
            package_full_path, err = self.download_package(pkg)

            if err:
                # I observed a zero-length file left on error,
                # which prevents cleanup later. Fix it:
                try:
                    if package_full_path is not None:
                        os.unlink(package_full_path)
                except OSError:
                    pass
                print(_("Downloading package {0} failed").format(pkg))
            else:
                assert err is None
                assert isinstance(package_full_path, str)

                unpack_result = unpack_rpm(package_full_path,
                                           files,
                                           self.tmpdir,
                                           self.cachedir,
                                           exact_files=download_exact_files)

                if unpack_result == RETURN_FAILURE:
                    # recursively delete the temp dir on failure
                    print(_("Unpacking failed, aborting download..."))

                    s = os.stat(self.cachedir)
                    abrt = pwd.getpwnam("abrt")
                    if s.st_gid != abrt.pw_gid:
                        print(_("'{0}' must be owned by group abrt. "
                                "Please run '# chown -R :abrt {0}' "
                                "to fix the issue.").format(self.cachedir))

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
                error_msg(_("Can't remove {0}, probably contains an error log")
                          .format(self.tmpdir))

        return RETURN_OK


def build_ids_to_paths(prefix: str, build_ids: List[str]) -> List[str]:
    """
    Returns the list of posible locations of debug files
    for the supplied build-ids.
    """

    paths = ["{0}/usr/lib/debug/.build-id/{1}/{2}.debug"
             .format(prefix, b_id[:2], b_id[2:])
             for b_id in build_ids]
    paths += ["{0}/usr/lib/.build-id/{1}/{2}.debug"
              .format(prefix, b_id[:2], b_id[2:])
              for b_id in build_ids]

    return paths

# beware this finds only missing libraries, but not the executable itself ..


def filter_installed_debuginfos(build_ids: List[str], cache_dirs: List[str]) \
        -> List[str]:
    """
    Find debuginfo files corresponding to the given build IDs that are
    missing on the system and need to be installed.

    Arguments:
        build_ids - string containing build ids
        cache_dirs - list of cache directories

    Returns:
        List of missing debuginfo files.
    """

    files = build_ids_to_paths("", build_ids)
    missing = []

    # First round: Look for debuginfo files with no prefix, i.e. in /usr/lib.
    for debuginfo_path in files:
        if os.path.exists(debuginfo_path):
            log2("found: %s", debuginfo_path)
            continue
        log2("not found: %s", debuginfo_path)
        missing.append(debuginfo_path)

    if missing:
        files = missing
        missing = []
    else:  # nothing is missing, we can stop looking
        return []

    # Second round: Look for debuginfo files missed in first round in cache
    # directories.
    for cache_dir in cache_dirs:
        log2("looking in %s" % cache_dir)
        for debuginfo_path in files:
            cache_debuginfo_path = os.path.join(cache_dir, debuginfo_path)
            if os.path.exists(cache_debuginfo_path):
                log2("found: %s", cache_debuginfo_path)
                continue
            log2("not found: %s", debuginfo_path)
            missing.append(debuginfo_path)

        # Only look for files that weren't found yet in subsequent iterations.
        if missing:
            files = missing
            missing = []
        else:  # nothing is missing, we can stop looking
            return []

    return files
