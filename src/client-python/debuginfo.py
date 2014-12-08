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

from yum import _, YumBase
from yum.callbacks import DownloadBaseCallback
from yum.Errors import YumBaseError

from reportclient import (_, log1, log2, RETURN_OK, RETURN_FAILURE,
                          RETURN_CANCEL_BY_USER, verbose, ask_yes_no,
                          error_msg)

TMPDIR=""

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
def unpack_rpm(package_file_name, files, tmp_dir, destdir, keeprpm, exact_files=False):
    """
    Unpacks a single rpm located in tmp_dir into destdir.

    Arguments:
        package_file_name - name of the rpm file
        files - files to extract from the rpm
        tmp_dir - temporary directory where the rpm file is located
        destdir - destination directory for the rpm package extraction
        keeprpm - check if the user wants to delete rpms from the tmp directory
        exact_files - extract only specified files

    Returns:
        RETURN_FAILURE in case of a serious problem
    """

    package_full_path = tmp_dir + "/" + package_file_name
    log1("Extracting %s to %s", package_full_path, destdir)
    log2("%s", files)
    print(_("Extracting cpio from {0}").format(package_full_path))
    unpacked_cpio_path = tmp_dir + "/unpacked.cpio"
    try:
        unpacked_cpio = open(unpacked_cpio_path, 'wb')
    except IOError, ex:
        print _("Can't write to '{0}': {1}").format(unpacked_cpio_path, ex)
        return RETURN_FAILURE

    rpm2cpio = Popen(["rpm2cpio", package_full_path],
                       stdout = unpacked_cpio, bufsize = -1)
    retcode = rpm2cpio.wait()

    if retcode == 0:
        log1("cpio written OK")
        if not keeprpm:
            log1("keeprpms = False, removing %s", package_full_path)
            #print _("Removing temporary rpm file")
            os.unlink(package_full_path)
    else:
        unpacked_cpio.close()
        print _("Can't extract package '{0}'").format(package_full_path)
        return RETURN_FAILURE

    # close the file
    unpacked_cpio.close()
    # and open it for reading
    unpacked_cpio = open(unpacked_cpio_path, 'rb')

    print _("Caching files from {0} made from {1}").format("unpacked.cpio", package_file_name)

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
        print _("Can't extract files from '{0}'").format(unpacked_cpio_path)
        return RETURN_FAILURE

def clean_up():
    """
    Removes the temporary directory.
    """

    if TMPDIR:
        try:
            shutil.rmtree(TMPDIR)
        except OSError, ex:
            if ex.errno != errno.ENOENT:
                error_msg(_("Can't remove '{0}': {1}").format(TMPDIR, ex))

class MyDownloadCallback(DownloadBaseCallback):
    """
    This class serves as a download progress handler for yum's progress bar.
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
        DownloadBaseCallback.__init__(self)

    def updateProgress(self, name, frac, fread, ftime):
        """
        A method used to update the progress

        Arguments:
            name - filename
            frac - progress fracment (0 -> 1)
            fread - formated string containing BytesRead
            ftime - formated string containing remaining or elapsed time
        """

        pct = int(frac * 100)
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
                print (_("Downloading ({0} of {1}) {2}: {3:3}%").format(
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
                print (_("Downloading ({0} of {1}) {2}: {3:3}%").format(
                        self.downloaded_pkgs + 1, self.total_pkgs, name, pct
                        )
                )
                self.last_time = t
                if pct == 100:
                    self.last_time = 0

        sys.stdout.flush()

def downloadErrorCallback(callBackObj):
    """
    A callback function for mirror errors.
    """

    print _("Problem '{0!s}' occured while downloading from mirror: '{1!s}'. Trying next one").format(
        callBackObj.exception, callBackObj.mirror)
    # explanation of the return value can be found here:
    # /usr/lib/python2.7/site-packages/urlgrabber/mirror.py
    return {'fail':0}

class DebugInfoDownload(YumBase):
    """
    This class is used to manage download of debuginfos.
    """

    def __init__(self, cache, tmp, repo_pattern="*debug*", keep_rpms=False,
                 noninteractive=True):
        self.old_stdout = -1
        self.cachedir = cache
        self.tmpdir = tmp
        global TMPDIR
        TMPDIR = tmp
        self.keeprpms = keep_rpms
        self.noninteractive = noninteractive
        self.repo_pattern=repo_pattern
        YumBase.__init__(self)
        self.mute_stdout()
        #self.conf.cache = os.geteuid() != 0
        # Setup yum (Ts, RPM db, Repo & Sack)
        # doConfigSetup() takes some time, let user know what we are doing
        print _("Initializing yum")
        try:
            # Saw this exception here:
            # cannot open Packages index using db3 - Permission denied (13)
            # yum.Errors.YumBaseError: Error: rpmdb open failed
            self.doConfigSetup()
        except YumBaseError, ex:
            self.unmute_stdout()
            print _("Error initializing yum (YumBase.doConfigSetup): '{0!s}'").format(ex)
            #return 1 - can't do this in constructor
            exit(1)
        self.unmute_stdout()

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
                print "ERR: unmute called without mute?"

    @ensure_abrt_uid
    def setup_tmp_dirs(self):
        if not os.path.exists(self.tmpdir):
            try:
                os.makedirs(self.tmpdir)
            except OSError, ex:
                print "Can't create tmpdir: %s" % ex
                return RETURN_FAILURE
        if not os.path.exists(self.cachedir):
            try:
                os.makedirs(self.cachedir)
            except OSError, ex:
                print "Can't create cachedir: %s" % ex
                return RETURN_FAILURE

        return RETURN_OK

    # return value will be used as exitcode. So 0 = ok, !0 - error
    def download(self, files, download_exact_files=False):
        """
        Downloads rpms into a temporary directory

        Arguments:
            package_files_dict - a dict containing {pkg: file list} entries
            total_pkgs - total number of packages to download
            download_exact_files - extract only specified files

        Returns:
            RETURN_OK if all goes well.
            RETURN_FAILURE in case it cannot set up either of the directories.
        """

        installed_size = 0
        total_pkgs = 0
        todownload_size = 0
        downloaded_pkgs = 0
        # nothing to download?
        if not files:
            return RETURN_FAILURE

        #if verbose == 0:
        #    # this suppress yum messages about setting up repositories
        #    mute_stdout()

        # make yumdownloader work as non root user
        if not self.setCacheDir():
            print _("Error: can't make cachedir, exiting")
            return RETURN_FAILURE

        # disable all not needed
        for repo in self.repos.listEnabled():
            try:
                repo.close()
                self.repos.disableRepo(repo.id)
            except YumBaseError, ex:
                print _("Can't disable repository '{0!s}': {1!s}").format(repo.id, str(ex))

        # This takes some time, let user know what we are doing
        print _("Setting up yum repositories")
        # setting-up repos one-by-one, so we can skip the broken ones...
        # this helps when users are using 3rd party repos like rpmfusion
        # in rawhide it results in: Can't find valid base url...
        for r in self.repos.findRepos(pattern=self.repo_pattern):
            try:
                rid = self.repos.enableRepo(r.id)
                self.repos.doSetup(thisrepo=str(r.id))
                log1("enabled repo %s", rid)
                setattr(r, "skip_if_unavailable", True)
                # yes, we want async download, otherwise our progressCallback
                # is not called and the internal yum's one  is used,
                # which causes artifacts on output
                try:
                    setattr(r, "_async", False)
                except (NameError, AttributeError), ex:
                    print ex
                    print _("Can't disable async download, the output might contain artifacts!")
            except YumBaseError, ex:
                print _("Can't setup {0}: {1}, disabling").format(r.id, ex)
                self.repos.disableRepo(r.id)

        # This is somewhat "magic", it unpacks the metadata making it usable.
        # Looks like this is the moment when yum talks to remote servers,
        # which takes time (sometimes minutes), let user know why
        # we have "paused":
        print _("Looking for needed packages in repositories")
        try:
            self.repos.populateSack(mdtype='metadata', cacheonly=1)
        except YumBaseError, ex:
            print _("Error retrieving metadata: '{0!s}'").format(ex)
            #we don't want to die here, some metadata might be already retrieved
            # so there is a chance we already have what we need
            #return 1

        try:
            # Saw this exception here:
            # raise Errors.NoMoreMirrorsRepoError, errstr
            # NoMoreMirrorsRepoError: failure:
            # repodata/7e6632b82c91a2e88a66ad848e231f14c48259cbf3a1c3e992a77b1fc0e9d2f6-filelists.sqlite.bz2
            # from fedora-debuginfo: [Errno 256] No more mirrors to try.
            self.repos.populateSack(mdtype='filelists', cacheonly=1)
        except YumBaseError, ex:
            print _("Error retrieving filelists: '{0!s}'").format(ex)
            # we don't want to die here, some repos might be already processed
            # so there is a chance we already have what we need
            #return 1

        #if verbose == 0:
        #    # re-enable the output to stdout
        #    unmute_stdout()

        not_found = []
        package_files_dict = {}
        for debuginfo_path in files:
            log2("yum whatprovides %s", debuginfo_path)
            pkg = self.pkgSack.searchFiles(debuginfo_path)
            # sometimes one file is provided by more rpms, we can use either of
            # them, so let's use the first match
            if pkg:
                if pkg[0] in package_files_dict.keys():
                    package_files_dict[pkg[0]].append(debuginfo_path)
                else:
                    package_files_dict[pkg[0]] = [debuginfo_path]
                    todownload_size += float(pkg[0].size)
                    installed_size += float(pkg[0].installedsize)
                    total_pkgs += 1

                log2("found pkg for %s: %s", debuginfo_path, pkg[0])
            else:
                log2("not found pkg for %s", debuginfo_path)
                not_found.append(debuginfo_path)

        # connect our progress update callback
        dnlcb = MyDownloadCallback(total_pkgs)
        self.repos.setProgressBar(dnlcb)
        self.repos.setMirrorFailureCallback(downloadErrorCallback)

        if verbose != 0 or len(not_found) != 0:
            print _("Can't find packages for {0} debuginfo files").format(len(not_found))
        if verbose != 0 or total_pkgs != 0:
            print _("Packages to download: {0}").format(total_pkgs)
            question = _("Downloading {0:.2f}Mb, installed size: {1:.2f}Mb. Continue?").format(
                         todownload_size / (1024*1024),
                         installed_size / (1024*1024)
                        )
            if self.noninteractive == False and not ask_yes_no(question):
                print _("Download cancelled by user")
                return RETURN_CANCEL_BY_USER
            # set up tmp and cache dirs so that we can check free space in both
            retval = self.setup_tmp_dirs()
            if retval != RETURN_OK:
                return retval
            # check if there is enough free space in both tmp and cache
            res = os.statvfs(self.tmpdir)
            tmp_space = float(res.f_bsize * res.f_bavail) / (1024*1024)
            if (todownload_size / (1024*1024)) > tmp_space:
                question = _("Warning: Not enough free space in tmp dir '{0}'"
                             " ({1:.2f}Mb left). Continue?").format(
                    self.tmpdir, tmp_space)
                if not self.noninteractive and not ask_yes_no(question):
                    print _("Download cancelled by user")
                    return RETURN_CANCEL_BY_USER
            res = os.statvfs(self.cachedir)
            cache_space = float(res.f_bsize * res.f_bavail) / (1024*1024)
            if (installed_size / (1024*1024)) > cache_space:
                question = _("Warning: Not enough free space in cache dir "
                             "'{0}' ({1:.2f}Mb left). Continue?").format(
                    self.cachedir, cache_space)
                if not self.noninteractive and not ask_yes_no(question):
                    print _("Download cancelled by user")
                    return RETURN_CANCEL_BY_USER

        for pkg, files in package_files_dict.items():
            dnlcb.downloaded_pkgs = downloaded_pkgs
            repo.cache = 0
            remote = pkg.returnSimple('relativepath')
            local = os.path.basename(remote)
            retval = self.setup_tmp_dirs()
            # continue only if the tmp dirs are ok
            if retval != RETURN_OK:
                return retval

            remote_path = pkg.returnSimple('remote_url')
            # check if the pkg is in a local repo and copy it if it is
            err = None
            if remote_path.startswith('file:///'):
                pkg_path = remote_path[7:]
                log2("copying from local repo: %s", remote)
                try:
                    shutil.copy(pkg_path, self.tmpdir)
                except OSError, ex:
                    print _("Cannot copy file '{0}': {1}").format(pkg_path, ex)
                    continue
            else:
                # pkg is in a remote repo, we need to download it to tmpdir
                local = os.path.join(self.tmpdir, local)
                pkg.localpath = local # Hack: to set the localpath we want
                err = self.downloadPkgs(pkglist=[pkg])
            # normalize the name
            # just str(pkg) doesn't work because it can have epoch
            pkg_nvra = pkg.name + "-" + pkg.version + "-" + pkg.release + "." + pkg.arch
            package_file_name = pkg_nvra + ".rpm"
            if err:
                # I observed a zero-length file left on error,
                # which prevents cleanup later. Fix it:
                try:
                    os.unlink(self.tmpdir + "/" + package_file_name)
                except OSError:
                    pass
                print (_("Downloading package {0} failed").format(pkg))
            else:
                unpack_result = unpack_rpm(package_file_name, files, self.tmpdir,
                                           self.cachedir, self.keeprpms,
                                           exact_files=download_exact_files)
                if unpack_result == RETURN_FAILURE:
                    # recursively delete the temp dir on failure
                    print _("Unpacking failed, aborting download...")
                    clean_up()
                    return RETURN_FAILURE

            downloaded_pkgs += 1

        if not self.keeprpms and os.path.exists(self.tmpdir):
            # Was: "All downloaded packages have been extracted, removing..."
            # but it was appearing even if no packages were in fact extracted
            # (say, when there was one package, and it has download error).
            print (_("Removing {0}").format(self.tmpdir))
            try:
                os.rmdir(self.tmpdir)
            except OSError:
                error_msg(_("Can't remove %s, probably contains an error log").format(self.tmpdir))

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
