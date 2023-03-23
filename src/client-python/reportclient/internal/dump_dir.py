#   Copyright (C) 2009  Zdenek Prikryl (zprikryl@redhat.com)
#   Copyright (C) 2009  Red Hat inc.

#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.

#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.

#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import errno
import grp
import os
import pwd
import re
import stat
import sys
import tarfile
from time import sleep, time
from typing import Dict, List

import reportclient.internal.const as const
from reportclient.internal.reported_to import ReportedTo

DD_FAIL_QUIETLY_ENOENT = 1 << 0
DD_FAIL_QUIETLY_EACCES = 1 << 1
# Open symlinks. dd_* funcs don't open symlinks by default */
DD_OPEN_FOLLOW = 1 << 2
DD_OPEN_READONLY = 1 << 3
DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE = 1 << 4
DD_DONT_WAIT_FOR_LOCK = 1 << 5
# Create the new dump directory with parent directories (mkdir -p)*/
DD_CREATE_PARENTS = 1 << 6
# Initializes internal data, opens file descriptors and returns the
# structure. This flag is useful for testing whether a directory
# exists and to perform stat operations.
DD_OPEN_FD_ONLY = 1 << 7

# Locking logic:
#
# The directory is locked by creating a symlink named .lock inside it,
# whose value (where it "points to") is the pid of locking process.
# We use symlink, not an ordinary file, because symlink creation
# is an atomic operation.
#
# There are two cases where after .lock creation, we might discover
# that directory is not really free:
# * another process just created new directory, but didn't manage
#   to lock it before us.
# * another process is deleting the directory, and we managed to sneak in
#   and create .lock after it deleted all files (including .lock)
#   but before it rmdir'ed the empty directory.
#
# Both these cases are detected by the fact that file named "time"
# is not present (it must be present in any valid dump dir).
# If after locking the dir we don't see time file, we remove the lock
# at once and back off. What happens in concurrent processes
# we interfered with?
# * "create new dump dir" process just re-tries locking.
# * "delete dump dir" process just retries rmdir.
#
# There is another case when we don't find time file:
# when the directory is not really a *dump* dir - user gave us
# an ordinary directory name by mistake.
# We detect it by bailing out of "lock, check time file; sleep
# and retry if it doesn't exist" loop using a counter.
#
# To make locking work reliably, it's important to set timeouts
# correctly. For example, dd_create should retry locking
# its newly-created directory much faster than dd_opendir
# tries to lock the directory it tries to open.


# How long to sleep between "symlink fails with EEXIST,
# readlink fails with ENOENT" tries. Someone just unlocked the dir.
# We never bail out in this case, we retry forever.
# The value can be really small:
SYMLINK_RETRY_USLEEP = 10*1000

# How long to sleep when lock file with valid pid is seen by dd_opendir
# (we are waiting for other process to unlock or die):
WAIT_FOR_OTHER_PROCESS_USLEEP = 500*1000

# How long to sleep when lock file with valid pid is seen by dd_create
# (some idiot jumped the gun and locked the dir we just created).
# Must not be the same as WAIT_FOR_OTHER_PROCESS_USLEEP (we depend on this)
# and should be small (we have the priority in locking, this is OUR dir):
CREATE_LOCK_USLEEP = 10*1000

# How long to sleep after we locked a dir, found no time file
# (either we are racing with someone, or it's not a dump dir)
# and unlocked it;
# and after how many tries to give up and declare it's not a dump dir:
NO_TIME_FILE_USLEEP = 50*1000
NO_TIME_FILE_COUNT = 10

# How long to sleep after we unlocked an empty dir, but then rmdir failed
# (some idiot jumped the gun and locked the dir we are deleting);
# and after how many tries to give up:
RMDIR_FAIL_USLEEP = 10*1000
RMDIR_FAIL_COUNT = 50

# A sub-directory of a dump directory where the meta-data such as owner are
# stored. The meta-data directory must have same owner, group and mode as its
# parent dump directory. It is not a fatal error, if the meta-data directory
# does not exist (backward compatibility).
META_DATA_DIR_NAME = ".libreport"
META_DATA_FILE_OWNER = "owner"

# Try to create meta-data dir if it does not exist
DD_MD_GET_CREATE = 1 << 0

# TODO: This is set at build time in C version
DUMP_DIR_OWNED_BY_USER = True

# Owner of trusted elements
DD_G_SUPER_USER_UID = 0

# Group of new dump directories
DD_G_FS_GROUP_GID = -1


def dd_mode_to_dir_mode(mode):
    """
    a little trick to copy read bits from file mode to exec bit of dir mode
    * mode of dump directory is in the form of 640 (no X) because we create
      files more often then we play with directories
    * so if we want to get real mode of the directory we have to copy the read
      bits
    """
    return mode | ((mode & 0o40444) >> 2)


def is_correct_filename(filename):
    if filename in ['.', '..', ''] or not filename.isprintable():
        return False
    if filename.find('/') > -1:
        return False
    if len(filename) > 61:  # No idea why 61, that's what the C code was using.
        return False
    return True


def dd_validate_element_name(name):
    """
    A valid dump dir element name is correct filename and is not a name of any
    internal file or directory.
    """
    return is_correct_filename(name) and META_DATA_DIR_NAME != name


def dd_init():
    """
    struct dump_dir {
        char *dd_dirname;
        DIR *next_dir;
        int locked;
        uid_t dd_uid;
        gid_t dd_gid;
        /* mode of saved files */
        mode_t mode;
        time_t dd_time;
        char *dd_type;

        /* In case of recursive locking the first caller owns the lock and is
         * responsible for unlocking. The consecutive dd_lock() callers acquire the
         * lock but are not able to unlock the dump directory.
         */
        int owns_lock;
        int dd_fd;
        /* Never use this member directly, it is intialized on demand in
         * dd_get_meta_data_dir_fd()
         */
        int dd_md_fd;
    };
    """
    return {'dd_dirname': '',
            'next_dir': None,
            'locked': False,
            'dd_uid': -1,
            'dd_gid': -1,
            'mode': 0,
            'dd_time': -1,
            'dd_type': '',
            'owns_lock': False,
            'dd_fd': -1,
            'dd_md_fd': -1}


def dd_close_meta_data_dir(dd: Dict):
    if dd['dd_md_fd'] < 0:
        return

    os.close(dd['dd_md_fd'])
    dd['dd_md_fd'] = -1


def load_text_from_file_descriptor(fd):
    with os.fdopen(fd, 'r') as file:
        file_content = file.read()

    buf_content = ''

    file_content = file_content.replace('\r', '\n')
    file_content = file_content.replace('\0', ' ')
    file_content = file_content.rstrip(' \t')
    for ch in file_content:
        if (ch.isspace() or ch >= ' '):
            buf_content += ch

    # If file contains exactly one '\n' and it is at the end, remove it.
    # This enables users to use simple "echo blah >file" in order to create
    # short string items in dump dirs.

    num_newlines = len(re.findall(r'\n', buf_content))
    if not num_newlines:
        return buf_content
    if num_newlines == 1 and buf_content.endswith('\n'):
        return buf_content.rstrip('\n')
    if not buf_content.endswith('\n'):
        buf_content += '\n'
    return buf_content


def dd_item_stat(dd: Dict, name: str):
    if not dd_validate_element_name(name):
        return -errno.EINVAL

    statbuf = os.stat(name, dir_fd=dd['dd_fd'], follow_symlinks=False)

    if not stat.S_ISREG(statbuf.st_mode):
        return -errno.EMEDIUMTYPE

    return 0


def dd_clear_next_file(dd: Dict):
    if not dd['next_dir']:
        return

    dd['next_dir'].close()
    dd['next_dir'] = None


class DumpDir:
    def __init__(self, logger):
        self.logger = logger
        self.reported_to = ReportedTo(logger)

    def get_no_owner_uid(self):
        """
        nobody user should not own any file
        """
        try:
            pw = pwd.getpwnam('nobody')
        except KeyError:
            self.logger.error("can't get nobody's uid")
            return None

        return pw.pw_uid

    def secure_openat_read(self, dir_fd: int, filename: str):
        """
        Opens the file in the three following steps:
        1. open the file with O_PATH (get a file descriptor for operations with
           inode) and O_NOFOLLOW (do not dereference symbolic links)
        2. stat the resulting file descriptor and fail if the opened file is not a
           regular file or if the number of links is greater than 1 (that means that
           the inode has more names (hard links))
        3. "re-open" the file descriptor retrieved in the first step with O_RDONLY
           by opening /proc/self/fd/$fd (then close the former file descriptor and
           return the new one).
        """
        if '/' in filename:
            self.logger.error("Path must be file name without directory: '%s'", filename)
            return -errno.EFAULT

        try:
            path_fd = os.open(filename, os.O_PATH | os.O_NOFOLLOW, dir_fd=dir_fd)
        except OSError as exc:
            return -exc.errno

        try:
            path_sb = os.stat(path_fd)
        except OSError:
            self.logger.error("stat")
            os.close(path_fd)
            return -errno.EINVAL

        if not stat.S_ISREG(path_sb.st_mode) or path_sb.st_nlink > 1:
            self.logger.info("Path isn't a regular file or has more links (%lu)", path_sb.st_nlink)
            os.close(path_fd)
            return -errno.EINVAL

        reopen_buf = f'/proc/self/fd/{path_fd}'

        try:
            fd = os.open(reopen_buf, os.O_RDONLY)
        except OSError as exc:
            fd = -exc.errno
        os.close(path_fd)

        return fd

    def read_number_from_file_at(self, dir_fd: int, filename: str):
        fd = self.secure_openat_read(dir_fd, filename)
        if fd < 0:
            self.logger.info("Can't open '%s'", filename)
            return fd

        error = 0
        value = None

        total_read = 33
        # because reasonable numbers usually don't have more than 32 digits
        # allow to read one extra Byte to be able to identify longer text

        value_buf = os.read(fd, total_read).decode()

        if not value_buf:
            self.logger.info("Can't read from '%s'", filename)
            error = 1

        elif len(value_buf) == total_read:
            self.logger.info("File '%s' is probably too long to be a valid number ")
            error = 1

        if error:
            os.close(fd)
            return None

        # Our tools don't put trailing newline into one line files,
        # but we allow such format too:
        value_buf = value_buf.rstrip('\n')

        try:
            if '.' in value_buf:
                value = float(value_buf)
            else:
                value = int(value_buf)
        except ValueError:
            self.logger.info("File '%s' doesn't contain a valid number ('%s')",
                             filename, value_buf)
        os.close(fd)
        return value

    def parse_time_file_at(self, dir_fd: int, filename: str):
        """
        Returns value less than 0 if the file is not readable or
        if the file doesn't contain valid unix time stamp.

        Any possible failure will be logged.
        """
        value = self.read_number_from_file_at(dir_fd, filename)
        if value is None:
            return -1
        return value

    def create_symlink_lockfile_at(self, dir_fd, lock_file, pid):
        """
        Return values:
        -1: error (in this case, errno is 0 if error message is already logged)
         0: failed to lock (someone else has it locked)
         1: success

        """
        while True:
            try:
                os.symlink(pid, lock_file, dir_fd=dir_fd)
            except OSError as exc:
                if sys.exc_info()[0] != FileExistsError:
                    if sys.exc_info()[0] not in [FileNotFoundError, NotADirectoryError, PermissionError]:
                        self.logger.error("Can't create lock file '%s'", lock_file)
                    return -1

                try:
                    pid_buf = os.readlink(lock_file, dir_fd=dir_fd)
                except OSError:
                    if sys.exc_info()[0] == FileNotFoundError:
                        # Looks like lock_file was deleted
                        sleep(SYMLINK_RETRY_USLEEP / 1000000)  # avoid CPU eating loop
                        continue
                    self.logger.error("Can't read lock file '%s'", lock_file)
                    return -1

                if pid_buf == pid:
                    self.logger.warning("Lock file '%s' is already locked by us", lock_file)
                    raise BlockingIOError from exc  # EALREADY
                if pid_buf.isdigit():
                    pid_str = f"/proc/{pid_buf}"
                    if os.access(pid_str, os.F_OK):
                        self.logger.warning("Lock file '%s' is locked by process %s", lock_file, pid_buf)
                        return 0
                    self.logger.warning("Lock file '%s' was locked by process %s, but it crashed?", lock_file, pid_buf)
                # The file may be deleted by now by other process. Ignore ENOENT
                try:
                    os.unlink(lock_file, dir_fd=dir_fd)
                except OSError:
                    if sys.exc_info()[0] != FileNotFoundError:
                        self.logger.error("Can't remove stale lock file '%s'", lock_file)
                        return -1
            break

        self.logger.info("Locked '%s'", lock_file)
        return 1

    def dd_check(self, dd: Dict):
        dd['dd_time'] = self.parse_time_file_at(dd['dd_fd'], const.FILENAME_TIME)
        if dd['dd_time'] < 0:
            self.logger.debug("Missing file: %s", const.FILENAME_TIME)
            return const.FILENAME_TIME

        # Do not warn about missing 'type' file in non-verbose modes.

        # Handling of FILENAME_TYPE should be consistent with handling of
        # FILENAME_TIME in the function parse_time_file_at() where the missing
        # file message is printed by logging.info() (in a verbose mode).

        load_flags = DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE
        g_verbose = os.environ.get('ABRT_VERBOSE') or 0
        if int(g_verbose) < 1:
            load_flags |= DD_FAIL_QUIETLY_ENOENT

        dd['dd_type'] = self.load_text_file(os.path.join(dd['dd_dirname'],
                                                         const.FILENAME_TYPE),
                                            load_flags)
        if not dd['dd_type']:
            self.logger.debug("Missing or empty file: %s", const.FILENAME_TYPE)
            return const.FILENAME_TYPE

        return None

    def dd_lock(self, dd: Dict, sleep_usec: int, flags: int):
        if dd['locked']:
            self.logger.error("Locking bug on '%s'", dd['dd_dirname'])
            sys.exit(1)

        pid_buf = str(os.getpid())

        count = NO_TIME_FILE_COUNT

        while True:
            while True:
                try:
                    ret = self.create_symlink_lockfile_at(dd['dd_fd'], ".lock", pid_buf)
                    if ret < 0:
                        return ret  # error
                    if ret > 0:
                        dd['owns_lock'] = True
                        break  # locked successfully
                except BlockingIOError:  # EALREADY
                    dd['owns_lock'] = False
                    break  # locked successfully
                if flags & DD_DONT_WAIT_FOR_LOCK:
                    raise BlockingIOError  # EAGAIN
                # Other process has the lock, wait for it to go away
                sleep(sleep_usec / 1000000)

            # Are we called by dd_opendir (as opposed to dd_create)?
            if sleep_usec == WAIT_FOR_OTHER_PROCESS_USLEEP:  # yes
                missing_file = self.dd_check(dd)
                # some of the required files don't exist. We managed to lock the directory
                # which was just created by somebody else, or is almost deleted
                # by delete_file_dir.
                # Unlock and back off.
                if missing_file:
                    if dd['owns_lock']:
                        try:
                            os.unlink(".lock", dir_fd=dd['dd_fd'])
                        except OSError:
                            self.logger.error("Can't remove file '.lock'")
                            sys.exit(1)

                    self.logger.info("Unlocked '%s' (missing or corrupted '%s' file)", dd['dd_dirname'], missing_file)
                    count -= 1
                    if count == 0 or flags & DD_DONT_WAIT_FOR_LOCK:
                        raise IsADirectoryError  # "this is an ordinary dir, not dump dir"
                    sleep(NO_TIME_FILE_USLEEP / 1000000)
                    continue
            break

        dd['locked'] = True
        return 0

    def dd_unlock(self, dd: Dict):
        if dd['locked']:
            if dd['owns_lock']:
                try:
                    os.unlink(".lock", dir_fd=dd['dd_fd'])
                except OSError:
                    self.logger.error("Can't remove file '.lock'")
                    sys.exit(1)

            dd['owns_lock'] = 0
            dd['locked'] = 0

            self.logger.info("Unlocked '%s/.lock'", dd['dd_dirname'])

    def dd_exist(self, dd: Dict, name: str):
        if not dd_validate_element_name(name):
            self.logger.error("Cannot test existence. '%s' is not a valid file name", name)
            sys.exit(1)
        try:
            buf = os.stat(name, dir_fd=dd['dd_fd'], follow_symlinks=False)
        except OSError:
            return False
        if stat.S_ISDIR(buf.st_mode) or stat.S_ISREG(buf.st_mode):
            return True
        return False

    def dd_close(self, dd: Dict):
        if not dd:
            return

        self.dd_unlock(dd)

        if dd['dd_fd'] >= 0:
            os.close(dd['dd_fd'])

        dd_close_meta_data_dir(dd)

        dd_clear_next_file(dd)

    def dd_create_subdir(self, dd_fd: int, dirname: str, dd_uid: int, dd_gid: int, dd_mode: int):
        try:
            os.mkdir(dirname, dd_mode, dir_fd=dd_fd)
        except (FileExistsError, FileNotFoundError):
            self.logger.error("Can't create directory '%s'", dirname)
            return -1

        while True:  # simulating goto cleanup :P
            try:
                dd_md_fd = os.open(dirname, os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=dd_fd)
            except OSError:
                self.logger.error("Can't open newly created directory '%s'", dirname)
                break
            if dd_uid != -1:
                try:
                    os.chown(dd_md_fd, dd_uid, dd_gid)
                except OSError:
                    self.logger.error("Can't change owner and group of '%s'", dirname)
                    os.close(dd_md_fd)
                    break

            # mkdir's mode (above) can be affected by umask, fix it
            try:
                os.chmod(dd_md_fd, dd_mode)
            except OSError:
                self.logger.error("Can't change mode of '%s'", dirname)
                os.close(dd_md_fd)
                break

            return dd_md_fd
        try:
            os.rmdir(dirname, dir_fd=dd_fd)
        except OSError:
            self.logger.error("Fialed to unlink '%s' while cleaning up after failure", dirname)
        return -1

    def dd_open_meta_data_dir(self, dd: Dict):
        """
        Opens the meta-data directory, checks its file system attributes and returns
        its file descriptor.

        The meta-data directory must have the same file system attributes as the
        parent dump directory in order to avoid unexpected situations and detects
        program errors (it is an error to modify bits of the dump directory and
        forgot to update the meta-data directory).

        Keep in mind that the old dump directories might miss the meta-data directory
        so the return value -ENOENT does not necessarily need to be fatal.
        """
        md_dir_name = os.path.join(dd['dd_dirname'], META_DATA_DIR_NAME)
        try:
            md_dir_fd = os.open(md_dir_name, os.O_NOFOLLOW)
        except OSError as exc:

            # ENOENT is not critical
            if exc.errno != errno.ENOENT:
                self.logger.warning("Can't open meta-data '%s'", META_DATA_DIR_NAME)
            else:
                self.logger.info("The dump dir doesn't contain '%s'", META_DATA_DIR_NAME)
            return -exc.errno

        try:
            md_sb = os.stat(md_dir_fd)
        except OSError:
            self.logger.debug("Can't stat '%s'", META_DATA_DIR_NAME)
            os.close(md_dir_fd)
            return -errno.EINVAL

        # Test only permission bits, ignore SUID, SGID, etc.
        md_mode = md_sb.st_mode & 0o40777
        dd_mode = dd_mode_to_dir_mode(dd['mode'])

        if (
            md_sb.st_uid != dd['dd_uid']
            or md_sb.st_gid != dd['dd_gid']
            or md_mode != dd_mode
        ):
            self.logger.debug("'%s' has different attributes than the dump dir, '%s'='%s', '%s'='%s', %s = %s",
                              META_DATA_DIR_NAME,
                              md_sb.st_uid,
                              dd['dd_uid'],
                              md_sb.st_gid,
                              dd['dd_gid'],
                              oct(md_mode)[3:],
                              oct(dd_mode)[3:])
            os.close(md_dir_fd)
            return -errno.EINVAL

        return md_dir_fd

    def dd_get_meta_data_dir_fd(self, dd: Dict, flags: int):
        """
        Returns a file descriptor to the meta-data directory. Can be configured to
        create the directory if it does not exist.

        This function enables lazy initialization of the meta-data directory.
        """
        if dd['dd_md_fd'] < 0:
            dd['dd_md_fd'] = self.dd_open_meta_data_dir(dd)

            if dd['dd_md_fd'] == -errno.ENOENT and (flags & DD_MD_GET_CREATE):
                dd['dd_md_fd'] = self.dd_create_subdir(dd['dd_fd'],
                                                       META_DATA_DIR_NAME,
                                                       dd['dd_uid'],
                                                       dd['dd_gid'],
                                                       dd_mode_to_dir_mode(dd['mode']))

        return dd['dd_md_fd']

    def dd_meta_data_save_text(self, dd: Dict, name: str, data: str):
        """
        Tries to safely overwrite the existing file.

        The functions writes the new value to a temporary file and if the temporary
        file is successfully created, then moves the tmp file to the old file name.

        If the meta-data directory does not exist, the function will try to create
        it.
        """
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        if not is_correct_filename(name):
            self.logger.error("Cannot save meta-data. '%s' is not a valid file name", name)
            sys.exit(1)

        dd_md_fd = self.dd_get_meta_data_dir_fd(dd, DD_MD_GET_CREATE)
        if dd_md_fd < 0:
            self.logger.error("Can't save meta-data: '%s'", name)
            return dd_md_fd

        tmp_name = f'~{name}.tmp'

        ret = -1
        if not self.save_binary_file_at(dd_md_fd, tmp_name,
                                        bytes(data, 'utf-8'), len(data),
                                        dd['dd_uid'], dd['dd_gid'],
                                        dd['mode']):
            return ret

        # man 2 rename

        # If newpath  already exists it will be atomically replaced (subject to a
        # few conditions; see ERRORS below), so that there is no point at which
        # another process attempting to access newpath will find it missing.

        try:
            os.rename(tmp_name, name, src_dir_fd=dd_md_fd, dst_dir_fd=dd_md_fd)
        except OSError as exc:
            self.logger.error("Failed to move temporary file '%s' to '%s'", tmp_name, name)
            raise sys.exc_info()[0] from exc

        ret = 0

        return ret

    def dd_set_owner(self, dd: Dict, owner: int):
        if owner == -1:
            owner = dd['dd_uid']

        ret = self.dd_meta_data_save_text(dd, META_DATA_FILE_OWNER, str(owner))
        if ret < 0:
            self.logger.error("The dump dir owner wasn't set to '%s'", str(owner))
        return ret

    def dd_set_no_owner(self, dd: Dict):
        no_owner_uid = self.get_no_owner_uid()
        if not no_owner_uid:
            return -1

        return self.dd_set_owner(dd, no_owner_uid)

    def fdreopen(self, dir_fd: int):
        """
        A helper function useful for traversing directories.

        DIR* d opendir(dir_fd); ... closedir(d); closes also dir_fd but we want to
        keep it opened.
        """
        try:
            opendir_fd = os.dup(dir_fd)
        except OSError as exc:
            self.logger.error('os.dup(dir_fd)')
            raise sys.exc_info()[0] from exc

        os.lseek(opendir_fd, 0, os.SEEK_SET)
        try:
            d_iter = os.scandir(opendir_fd)
        except OSError as exc:
            os.close(opendir_fd)
            self.logger.error('os.scandir(opendir_fd)')
            raise sys.exc_info()[0] from exc

        # 'opendir_fd' will be closed with 'd_iter'
        return d_iter

    def dd_sanitize_mode_meta_data(self, dd: Dict):
        """
        Sets attributes of the meta-data directory and its contents to the same
        attributes of the parent dump directory.
        """
        if not dd['locked']:
            self.logger.error("dd_sanitize_mode_meta_data(): dump_dir is not opened")
            sys.exit(1)  # bug

        dd_md_fd = self.dd_get_meta_data_dir_fd(dd, 0)
        if dd_md_fd < 0:
            return 0

        try:
            os.chmod(dd_md_fd, dd_mode_to_dir_mode(dd['mode']))
        except OSError as exc:
            self.logger.error("Failed to chmod meta-data sub-dir")
            return -exc.errno

        try:
            d = self.fdreopen(dd_md_fd)
        except OSError:
            return -1
        for dent in d:
            fd = self.secure_openat_read(d, dent.path)
            if fd >= 0:
                self.logger.debug("chmoding %s", dent.name)

                try:
                    os.chmod(fd, dd['mode'])
                except OSError:
                    self.logger.error("os.chmod('%s')", dent.name)
                    break
                os.close(fd)
        d.close()

        return 0

    def dd_chown_meta_data(self, dd: Dict, uid: int, gid: int):
        """
        Sets owner and group of the meta-data directory and its contents to the same
        attributes of the parent dump directory.
        """
        if not dd['locked']:
            self.logger.error("dd_chown_meta_data(): dump_dir is not opened")
            sys.exit(1)  # bug

        dd_md_fd = self.dd_get_meta_data_dir_fd(dd, 0)
        if dd_md_fd < 0:
            return 0

        res = 0
        try:
            os.chown(dd_md_fd, uid, gid)
        except OSError as exc:
            self.logger.error("Failed to chown meta-data sub-dir")
            return -exc.errno

        try:
            d = self.fdreopen(dd_md_fd)
        except OSError:
            return -1
        for dent in d:
            fd = self.secure_openat_read(d, dent.path)
            if fd >= 0:
                self.logger.debug("dd_chown_meta_data: chowning %s", dent.name)

                try:
                    os.chown(fd, uid, gid)
                except OSError as exc:
                    res = -exc.errno
                    self.logger.error("fchown('%s')", dent.name)
                    break
                os.close(fd)
        d.close()

        return res

    def dd_do_open(self, dd: Dict, directory: str, flags: int):
        if directory:
            dd['dd_dirname'] = directory.rstrip('/')
            # dd_do_open validates dd_fd
            try:
                if not os.path.isdir(dd['dd_dirname']):
                    raise NotADirectoryError(20, f"Not a directory: '{dd['dd_dirname']}'")
                dd['dd_fd'] = os.open(dd['dd_dirname'], os.O_NOFOLLOW)
                stat_buf = os.stat(dd['dd_fd'])
            except OSError:
                if sys.exc_info()[0] in [FileNotFoundError, NotADirectoryError, OSError]:
                    if not flags & DD_FAIL_QUIETLY_ENOENT:
                        self.logger.error("'%s' does not exist", dd['dd_dirname'])
                else:  # PermissionError
                    if not flags & DD_FAIL_QUIETLY_EACCES:
                        self.logger.error("Can't access '%s'", dd['dd_dirname'])
                self.dd_close(dd)
                return None

            # & 0666 should remove the executable bit
            dd['mode'] = stat_buf.st_mode & 0o40666

            # We want to have dd_uid and dd_gid always initialized. But we have to
            # initialize it in the way which does not prevent non-privileged user
            # from saving data in their dump directories.

            # Non-privileged users are not allowed to change the group to
            # 'abrt' so we have to use their GID.

            # If the caller is super-user, we have to use dd's fs owner and fs
            # group, because he can do everything and the data must be readable by
            # the real owner.

            # We always use fs uid, because non-privileged users must own the
            # directory and super-user must use fs owner.

            dd['dd_uid'] = stat_buf.st_uid

            # We use fs group only if the caller is super-user, because we want to
            # make sure non-privileged users can modify elements (libreport call
            # chown(dd_uid, dd_gid) after modifying an element) and the modified
            # elements do not have super-user's group.

            dd['dd_gid'] = os.getegid()
            if os.geteuid() == 0:
                dd['dd_gid'] = stat_buf.st_gid

            if flags & DD_OPEN_FD_ONLY:
                dd['dd_md_fd'] = self.dd_open_meta_data_dir(dd)
                return dd

        g_verbose = os.environ.get('ABRT_VERBOSE') or 0
        try:
            ret = self.dd_lock(dd, WAIT_FOR_OTHER_PROCESS_USLEEP, flags)
        except OSError:
            if sys.exc_info()[0] == IsADirectoryError:  # EISDIR
                # EISDIR: dd_lock can lock the dir, but it sees no time file there,
                # even after it retried many times. It must be an ordinary directory!

                # Without this check, e.g. abrt-action-print happily prints any current
                # directory when run without arguments, because its option -d DIR
                # defaults to "."!
                self.logger.error("'%s' is not a problem directory", dd['dd_dirname'])
                self.dd_close(dd)
                sys.exit(1)

            if sys.exc_info()[0] == BlockingIOError and (flags & DD_DONT_WAIT_FOR_LOCK):  # EAGAIN
                self.logger.debug("Can't access locked directory '%s'", dd['dd_dirname'])
                self.dd_close(dd)
                return None

            if sys.exc_info()[0] == PermissionError:  # EACCESS
                if int(g_verbose) >= 2:
                    self.logger.error("failed to lock dump directory '%s'", dd['dd_dirname'])
                self.dd_close(dd)
                return None

        if ret < 0:

            if not flags & DD_OPEN_READONLY:
                self.logger.debug("'%s' can't be opened for writing", dd['dd_dirname'])
                self.dd_close(dd)
                return None

            # Directory is not writable. If it seems to be readable,
            # return "read only" dd, not NULL

            # Does the directory have 'r' flag?
            if not os.access(".", os.R_OK, dir_fd=dd['dd_fd'], follow_symlinks=False):
                if int(g_verbose) >= 2:
                    self.logger.error("failed to lock dump directory '%s'", dd['dd_dirname'])
                self.dd_close(dd)
                return None

            # dd_check prints out good log messages
            if self.dd_check(dd):
                self.dd_close(dd)
                return None

            # The dd is opened in READONLY mode, continue.

        return dd

    def dd_opendir(self, directory: str, flags: int):
        dd = dd_init()
        return self.dd_do_open(dd, directory, flags)

    def dd_create_skeleton(self, directory: str, uid: int, mode: int, flags: int):
        """
        Create a fresh empty debug dump dir which is owned bu the calling user. If
        you want to create the directory with meaningful ownership you should
        consider using dd_create() function or you can modify the ownership
        afterwards by calling dd_reset_ownership() function.

        ABRT owns dump dir:
          We should not allow users to write new files or write into existing ones,
          but they should be able to read them.

          We set dir's gid to passwd(uid)->pw_gid parameter, and we set uid to
          abrt's user id. We do not allow write access to group. We can't set dir's
          uid to crashed applications's user uid because owner can modify dir's
          mode and ownership.

          Advantages:
          Safeness

          Disadvantages:
          This approach leads to stealing of directories because events requires
          write access to a dump directory and events are run under non root (abrt)
          user while reporting.

          This approach allows group members to see crashes of other members.
          Institutions like schools uses one common group for all students.

        User owns dump dir:
          We grant ownership of dump directories to the user (read/write access).

          We set set dir's uid to crashed applications's user uid, and we set gid to
          abrt's group id. We allow write access to group because we want to allow
          abrt binaries to process dump directories.

          Advantages:
          No disadvantages from the previous approach

          Disadvantages:
          In order to protect the system dump directories must be saved on
          noncritical filesystem (e.g. /tmp or /var/tmp).


        @param uid
          Crashed application's User Id

        We currently have only three callers:
         kernel oops hook: uid -> not saved, so everyone can steal and work with it
          this hook runs under 0:0

         ccpp hook: uid=uid of crashed user's binary
          this hook runs under 0:0

         create_dump_dir_from_problem_data() function:
          Currently known callers:
           abrt server: uid=uid of user's executable
            this runs under 0:0
            - clinets: python hook, ruby hook
           abrt dbus: uid=uid of user's executable
            this runs under 0:0
            - clients: setroubleshootd, abrt python
        """

        dir_mode = dd_mode_to_dir_mode(mode)
        dd = dd_init()

        dd['mode'] = mode

        # Unlike dd_opendir, can't use realpath: the directory doesn't exist yet,
        # realpath will always return NULL. We don't really have to:
        # dd_opendir(".") makes sense, dd_create(".") does not.

        directory = dd['dd_dirname'] = directory.rstrip('/')

        last_component = directory[directory.rfind('/')+1:]
        if last_component.startswith('.') or last_component.startswith('..'):
            # dd_create("."), dd_create(".."), dd_create("dir/."),
            # dd_create("dir/..") and similar are madness, refuse them.
            self.logger.error("Bad dir name '%s'", directory)
            self.dd_close(dd)
            return None

        # Was creating it with mode 0700 and user as the owner, but this allows
        # the user to replace any file in the directory, changing security-sensitive data
        # (e.g. "uid", "analyzer", "executable")

        try:
            if flags & DD_CREATE_PARENTS:
                os.makedirs(dd['dd_dirname'], dir_mode)
            else:
                os.mkdir(dd['dd_dirname'], dir_mode)
        except OSError:
            self.logger.error("Can't create directory '%s'", directory)
            self.dd_close(dd)
            return None

        try:
            dd['dd_fd'] = os.open(dd['dd_dirname'], os.O_DIRECTORY | os.O_NOFOLLOW)
        except OSError:
            self.logger.error("Can't open newly created directory '%s'", directory)
            self.dd_close(dd)
            return None

        try:
            stat_sb = os.stat(dd['dd_fd'])
        except OSError:
            self.logger.error("stat(%s)", dd['dd_dirname'])
            self.dd_close(dd)
            return None

        try:
            self.dd_lock(dd, CREATE_LOCK_USLEEP, flags=0)
        except (BlockingIOError, IsADirectoryError):
            self.dd_close(dd)
            return None

        # mkdir's mode (above) can be affected by umask, fix it
        try:
            os.chmod(dd['dd_fd'], dir_mode)
        except OSError:
            self.logger.error("Can't change mode of '%s'", directory)
            self.dd_close(dd)
            return None

        # Initiliaze dd_uid and dd_gid to sane values which reflect the reality.

        dd['dd_uid'] = stat_sb.st_uid
        dd['dd_gid'] = stat_sb.st_gid

        # Create META-DATA directory with real fs attributes which must be changed
        # in dd_reset_ownership(), when populating of a new dump directory is
        # done.

        # It allows daemons to create a dump directory, populate the directory as
        # root and then switch the ownership to the real user.
        dd['dd_md_fd'] = self.dd_create_subdir(dd['dd_fd'], META_DATA_DIR_NAME,
                                               dd['dd_uid'], dd['dd_gid'], dir_mode)
        if dd['dd_md_fd'] < 0:
            self.logger.error("Can't create meta-data directory")
            self.dd_close(dd)
            return None

        if self.dd_set_owner(dd, dd['dd_uid']) < 0:
            self.logger.debug("Failed to initialized 'owner'")
            self.dd_close(dd)
            return None

        if uid != -1:
            dd['dd_uid'] = 0
            dd['dd_gid'] = 0

        if DUMP_DIR_OWNED_BY_USER:
            # Check crashed application's uid
            try:
                pw = pwd.getpwuid(uid)
                dd['dd_uid'] = pw.pw_uid
            except KeyError:
                self.logger.error("User %lu does not exist, using uid 0", uid)

            if DD_G_FS_GROUP_GID == -1:
                # Get ABRT's group gid
                try:
                    gr = grp.getgrnam("abrt")
                    dd['dd_gid'] = gr.gr_gid
                except KeyError:
                    self.logger.error("Group 'abrt' does not exist, using gid 0")
            else:
                dd['dd_gid'] = DD_G_FS_GROUP_GID
        else:
            # Get ABRT's user uid
            try:
                pw = pwd.getpwnam("abrt")
                dd['dd_uid'] = pw.pw_uid
            except KeyError:
                self.logger.error("User 'abrt' does not exist, using uid 0")

            # Get crashed application's gid
            try:
                pw = pwd.getpwuid(uid)
                dd['dd_gid'] = pw.pw_gid
            except KeyError:
                self.logger.error("User %lu does not exist, using gid 0", uid)

        # Initialize dd_time to some sane value
        dd['dd_time'] = int(time())

        return dd

    def dd_reset_ownership(self, dd: Dict):
        """
        Resets ownership of the given directory to UID and GID according to values
        in dd_create_skeleton().
        """
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        ret = 0
        try:
            os.chown(dd['dd_fd'], dd['dd_uid'], dd['dd_gid'])
        except OSError as exc:
            self.logger.error("Can't change '%s' ownership to %lu:%lu", dd['dd_dirname'],
                              dd['dd_uid'], dd['dd_gid'])
            ret = -exc.errno

        if self.dd_chown_meta_data(dd, dd['dd_uid'], dd['dd_gid']) != 0:
            self.logger.error("Failed to reset ownership of meta-data")

        # We ignore failures above, so we will ignore failures here too.
        # The meta-data owner already exist (created by dd_create_skeleton).

        self.dd_set_owner(dd, dd['dd_uid'])

        return ret

    def dd_create(self, directory: str, uid: int, mode: int):
        """
        Calls dd_create_skeleton() and dd_reset_ownership().
        """
        dd = self.dd_create_skeleton(directory, uid, mode, DD_CREATE_PARENTS)
        if not dd:
            return None

        # ignore results
        self.dd_reset_ownership(dd)

        return dd

    def dd_create_basic_files(self, dd: Dict, uid: int, chroot_dir: str):
        timestamp = self.parse_time_file_at(dd['dd_fd'], const.FILENAME_TIME)
        if timestamp < 0:
            # first occurrence
            self.dd_save_text(dd, const.FILENAME_TIME, str(dd['dd_time']))
            # last occurrence
            self.dd_save_text(dd, const.FILENAME_LAST_OCCURRENCE, str(dd['dd_time']))
        else:
            dd['dd_time'] = timestamp

        # it doesn't make sense to create the uid file if uid == -1
        # and 'owner' is set since dd_create_skeleton
        if uid != -1:
            # Failure is not a problem here, because we still have the fs
            # attributes and there is only a little chance that the old value
            # gets lost.
            self.dd_set_owner(dd, uid)

            self.dd_save_text(dd, const.FILENAME_UID, str(uid))

        buf = os.uname()  # never fails
        # Check if files already exist in dumpdir as they might have
        # more relevant information about the problem

        if not self.dd_exist(dd, const.FILENAME_KERNEL):
            self.dd_save_text(dd, const.FILENAME_KERNEL, buf.release)
        if not self.dd_exist(dd, const.FILENAME_ARCHITECTURE):
            self.dd_save_text(dd, const.FILENAME_ARCHITECTURE, buf.machine)
        if not self.dd_exist(dd, const.FILENAME_HOSTNAME):
            self.dd_save_text(dd, const.FILENAME_HOSTNAME, buf.nodename)

        release = self.load_text_file("/etc/os-release",
                                      DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_OPEN_FOLLOW)
        if release:
            self.dd_save_text(dd, const.FILENAME_OS_INFO, release)

        if chroot_dir:
            self.copy_file_from_chroot(dd, const.FILENAME_OS_INFO_IN_ROOTDIR, chroot_dir, "/etc/os-release")

        # if release exists in dumpdir don't create it, but don't warn
        # if it doesn't
        # i.e: anaconda doesn't have /etc/{fedora,redhat}-release and trying to load it
        # results in errors: rhbz#725857

        release = self.dd_load_text_ext(dd, const.FILENAME_OS_RELEASE,
                                        DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE)

        if not release:
            release = self.load_text_file("/etc/system-release",
                                          DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_OPEN_FOLLOW)
            if not release:
                release = self.load_text_file("/etc/redhat-release",
                                              DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_OPEN_FOLLOW)
            if not release:
                release = self.load_text_file("/etc/SuSE-release", DD_OPEN_FOLLOW)

            # rstrip would probably be enough but that wasn't strictly
            # what the C code was doing
            if release.find('\n') > -1:
                release = release[:release.find('\n')]

            self.dd_save_text(dd, const.FILENAME_OS_RELEASE, release)
            if chroot_dir:
                self.copy_file_from_chroot(dd, const.FILENAME_OS_RELEASE_IN_ROOTDIR,
                                           chroot_dir, "/etc/system-release")

    def dd_sanitize_mode_and_owner(self, dd: Dict):
        # Don't sanitize if we aren't run under root:
        # we assume that during file creation (by whatever means,
        # even by "hostname >file" in abrt_event.conf)
        # normal umask-based mode setting takes care of correct mode,
        # and uid:gid is, of course, set to user's uid and gid.

        # For root operating on /var/spool/abrt/USERS_PROBLEM, this isn't true:
        # "hostname >file", for example, would create file OWNED BY ROOT!
        # This routine resets mode and uid:gid for all such files.

        if dd['dd_uid'] == -1:
            return

        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        self.dd_init_next_file(dd)
        while True:
            (short_name, _) = self.dd_get_next_file(dd)
            if not short_name:
                break
            # The current process has to have read access at least
            fd = self.secure_openat_read(dd['dd_fd'], short_name)
            if fd < 0:
                continue

            try:
                os.chmod(fd, dd['mode'])
            except OSError:
                self.logger.error("Can't change '%s/%s' mode to 0%o", dd['dd_dirname'],
                                  short_name, dd['mode'])

            try:
                os.chown(fd, dd['dd_uid'], dd['dd_gid'])
            except OSError:
                self.logger.error("Can't change '%s/%s' ownership to %lu:%lu", dd['dd_dirname'],
                                  short_name, dd['dd_uid'], dd['dd_gid'])

            os.close(fd)

        # No need to check return value, the functions print good messages.
        # There are two approaches for handling errors in libreport:
        # - print out a warning message and keep status quo
        # - terminate the process

        self.dd_sanitize_mode_meta_data(dd)
        self.dd_chown_meta_data(dd, dd['dd_uid'], dd['dd_gid'])

    def delete_file_dir(self, dir_fd: int, skip_lock_file: bool):
        try:
            new_dir_fd = self.fdreopen(dir_fd)
        except (FileNotFoundError, NotADirectoryError):
            return 0
        except OSError:
            return -1

        unlink_lock_file = False
        for dent in new_dir_fd:
            if skip_lock_file and dent.name == '.lock':
                unlink_lock_file = True
                continue
            err = 0
            try:
                os.unlink(dent.name, dir_fd=dir_fd)
            except IsADirectoryError:
                try:
                    subdir_fd = os.open(dent.d_name, os.O_DIRECTORY, dir_fd=dir_fd)
                except OSError:
                    self.logger.error("Can't open sub-dir'%s'", dent.name)
                    new_dir_fd.close()
                    return -1
                err = self.delete_file_dir(subdir_fd, skip_lock_file=False)
                os.close(subdir_fd)
                if err == 0:
                    os.rmdir(dent.name, dir_fd=dir_fd)
                else:
                    self.logger.error("Can't remove '%s'", dent.name)
                    new_dir_fd.close()
                    return -1
            except OSError:
                self.logger.error("Can't remove '%s'", dent.name)
                new_dir_fd.close()
                return -1

        # Here we know for sure that all files/subdirs we found via readdir
        # were deleted successfully. If rmdir below fails, we assume someone
        # is racing with us and created a new file.

        if unlink_lock_file:
            try:
                os.unlink('.lock', dir_fd=dir_fd)
            except OSError:
                self.logger.error("Can't remove file '.lock'")
                sys.exit(1)

        new_dir_fd.close()

        return 0

    def dd_delete_meta_data(self, dd: Dict):
        if not dd['locked']:
            self.logger.error("Can't remove meta-data of unlocked problem directory %s", dd['dd_dirname'])
            return -1

        dd_md_fd = self.dd_get_meta_data_dir_fd(dd, 0)
        if dd_md_fd < 0:
            return 0

        if self.delete_file_dir(dd_md_fd, skip_lock_file=True) != 0:
            self.logger.error("Can't remove meta-data from '%s'", META_DATA_DIR_NAME)
            return -2

        dd_close_meta_data_dir(dd)

        try:
            os.rmdir(META_DATA_DIR_NAME, dir_fd=dd['dd_fd'])
        except OSError:
            self.logger.error("Can't remove meta-data directory '%s'", META_DATA_DIR_NAME)
            return -3

        return 0

    def dd_delete(self, dd: Dict):
        retval = 0

        if not dd['locked']:
            self.logger.error("unlocked problem directory %s cannot be deleted", dd['dd_dirname'])
            retval = -1

        elif self.dd_delete_meta_data(dd) != 0:
            retval = -2

        elif self.delete_file_dir(dd['dd_fd'], skip_lock_file=True) != 0:
            self.logger.error("Can't remove contents of directory '%s'", dd['dd_dirname'])
            retval = -2

        else:
            cnt = RMDIR_FAIL_COUNT
            while cnt != 0:
                try:
                    os.rmdir(dd['dd_dirname'])
                except OSError:
                    # Someone locked the dir after unlink, but before rmdir.
                    # This "someone" must be dd_lock().
                    # It detects this (by seeing that there is no time file)
                    # and backs off at once. So we need to just retry rmdir,
                    # with minimal sleep.
                    cnt -= 1
                    sleep(RMDIR_FAIL_USLEEP / 1000000)
                    continue
                break

            if cnt == 0:
                self.logger.error("Can't remove directory '%s'", dd['dd_dirname'])
                retval = -3

            dd['locked'] = False  # delete_file_dir already removed .lock

        self.dd_close(dd)
        return retval

    def dd_chown(self, dd: Dict, new_uid: int):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        try:
            statbuf = os.stat(dd['dd_fd'])
        except OSError:
            self.logger.error("stat('%s')", dd['dd_dirname'])
            return 1

        try:
            pw = pwd.getpwuid(new_uid)
        except KeyError:
            self.logger.error("UID %ld is not found in user database", new_uid)
            return 1

        if DUMP_DIR_OWNED_BY_USER:
            owners_uid = pw.pw_uid
            groups_gid = statbuf.st_gid
        else:
            owners_uid = statbuf.st_uid
            groups_gid = pw.pw_gid

        chown_res = 0
        try:
            os.chown(dd['dd_fd'], owners_uid, groups_gid)
        except OSError:
            chown_res = 1
            self.logger.error("fchown('%s')", dd['dd_dirname'])
        else:
            self.dd_init_next_file(dd)
            while True:
                if chown_res:
                    break
                (short_name, _) = self.dd_get_next_file(dd)
                if not short_name:
                    break
                # The current process has to have read access at least
                fd = self.secure_openat_read(dd['dd_fd'], short_name)
                if fd < 0:
                    continue

                self.logger.debug("chowning %s", short_name)

                try:
                    os.chown(fd, owners_uid, groups_gid)
                except OSError:
                    chown_res = 1
                os.close(fd)

                if chown_res:
                    self.logger.error("os.chown('%s')", short_name)
                    break

        if chown_res == 0:
            chown_res = self.dd_chown_meta_data(dd, owners_uid, groups_gid)

        if chown_res == 0:
            dd['dd_uid'] = owners_uid
            dd['dd_gid'] = groups_gid
            chown_res = self.dd_set_owner(dd, dd['dd_uid'])

        return chown_res

    def load_text_file(self, path, flags):
        open_flags = os.O_RDONLY
        if not flags & DD_OPEN_FOLLOW:
            open_flags |= os.O_NOFOLLOW
        try:
            fd = os.open(path, open_flags)
        except OSError:
            if not flags & DD_FAIL_QUIETLY_ENOENT:
                self.logger.error("Can't open file '%s' for reading", path)
            if flags & DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE:
                return None
            return ''
        return load_text_from_file_descriptor(fd)

    def copy_file_from_chroot(self, dd: Dict, name: str, chroot_dir: str, file_path: str):
        if chroot_dir:
            chrooted_name = os.path.join(chroot_dir, file_path)
        else:
            chrooted_name = file_path

        data = self.load_text_file(chrooted_name,
                                   DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE | DD_OPEN_FOLLOW)
        if data:
            self.dd_save_text(dd, name, data)

    def create_new_file_at(self, dir_fd: int, omode: int, name: str, uid: int, gid: int, mode: int):
        assert not name.startswith('/')
        assert omode in [os.O_WRONLY, os.O_RDWR]

        try:
            os.unlink(name, dir_fd=dir_fd)
        except FileNotFoundError:
            pass  # The file doesn't have to exist, and that is fine

        # the mode is set by the caller, see dd_create() for security analysis
        try:
            fd = os.open(name, omode | os.O_EXCL | os.O_CREAT | os.O_NOFOLLOW, mode=mode, dir_fd=dir_fd)
        except OSError:
            self.logger.error("Can't open file '%s' for writing", name)
            return -1

        if uid != -1:
            try:
                os.fchown(fd, uid, gid)
            except OSError:
                self.logger.error("Can't change '%s' ownership to %lu:%lu", name, uid, gid)
                os.close(fd)
                return -1

        # O_CREAT in the open() call above causes that the permissions of the
        # created file are (mode & ~umask)

        # This is true only if we did create file. We are not sure we created it
        # in this case - it may exist already.
        try:
            os.chmod(fd, mode)
        except OSError:
            self.logger.error("Can't change mode of '%s'", name)
            os.close(fd)
            return -1

        return fd

    def save_binary_file_at(self, dir_fd: int, name: str, data: bytes, size: int, uid: int, gid: int, mode: int):
        fd = self.create_new_file_at(dir_fd, os.O_WRONLY, name, uid, gid, mode)
        if fd < 0:
            self.logger.error("Can't save file '%s'", name)
            return False

        # In the C version, libreport_full_write was here.
        # Let's just go with os.write and see what happens.
        r = os.write(fd, data)
        os.close(fd)
        if r != size:
            self.logger.error("Can't save file '%s'", name)
            return False

        return True

    def dd_load_text_ext(self, dd: Dict, name: str, flags: int):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        if not dd_validate_element_name(name):
            self.logger.error("Cannot load text. '%s' is not a valid file name", name)
            if flags & DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE:
                return None

            # TODO: WAS: libreport_xfunc_die()
            sys.exit(1)

        return self.load_text_file(os.path.join(dd['dd_dirname'], name), flags)

    def dd_load_text(self, dd: Dict, name: str):
        return self.dd_load_text_ext(dd, name,  0)

    def dd_save_text(self, dd: Dict, name: str, data: str):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        if not dd_validate_element_name(name):
            self.logger.error("Cannot save text. '%s' is not a valid file name", name)
            sys.exit(1)

        self.save_binary_file_at(dd['dd_fd'], name, bytes(data, 'utf-8'),
                                 len(data), dd['dd_uid'], dd['dd_gid'],
                                 dd['mode'])

    def dd_save_binary(self, dd: Dict, name: str, data: bytes, size: int):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        if not dd_validate_element_name(name):
            self.logger.error("Cannot save binary. '%s' is not a valid file name", name)
            sys.exit(1)

        self.save_binary_file_at(dd['dd_fd'], name, data, size, dd['dd_uid'], dd['dd_gid'], dd['mode'])

    def dd_delete_item(self, dd: Dict, name: str):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        if not dd_validate_element_name(name):
            self.logger.error("Cannot delete item. '%s' is not a valid file name", name)
            return -errno.EINVAL

        try:
            os.unlink(name, dir_fd=dd['dd_fd'])
        except IsADirectoryError:
            pass
        except OSError as exc:
            if sys.exc_info()[0] != FileNotFoundError:
                self.logger.error("Can't delete file '%s'", name)
                raise exc
        return 0

    def dd_open_item(self, dd: Dict, name: str, flag: int):
        if not dd_validate_element_name(name):
            self.logger.error("Cannot open item as FD. '%s' is not a valid file name", name)
            return -errno.EINVAL

        if flag == os.O_RDONLY:
            return os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=dd['dd_fd'])

        if not dd['locked']:
            self.logger.error("dump_dir is not locked")
            sys.exit(1)  # bug

        if flag == os.O_RDWR:
            return self.create_new_file_at(dd['dd_fd'], os.O_RDWR, name, dd['dd_uid'], dd['dd_gid'], dd['mode'])

        self.logger.error("invalid open item flag")
        return -errno.EOPNOTSUPP

    def _dd_get_next_file_dent(self, dd: Dict):
        if not dd['next_dir']:
            return None

        for dent in dd['next_dir']:
            if dent.is_file(follow_symlinks=False):
                return dent

        dd_clear_next_file(dd)
        return None

    def dd_compute_size(self, dd: Dict):
        retval = 0

        if not self.dd_init_next_file(dd):
            return -errno.EIO

        while True:
            dent = self._dd_get_next_file_dent(dd)
            if not dent:
                break
            try:
                statbuf = os.stat(dd['dd_fd'], follow_symlinks=False)
            except OSError as exc:
                retval = -exc.errno
                break
            retval += statbuf.st_size

        dd_clear_next_file(dd)
        return retval

    def dd_init_next_file(self, dd: Dict):
        try:
            opendir_fd = os.dup(dd['dd_fd'])
        except OSError:
            self.logger.error('dd_init_next_file: os.dup(dd_fd)')
            return None

        dd_clear_next_file(dd)

        os.lseek(opendir_fd, 0, os.SEEK_SET)
        try:
            dd['next_dir'] = os.scandir(opendir_fd)
        except OSError:
            self.logger.error("Can't open directory '%s'", dd['dd_dirname'])

        return dd['next_dir']

    def dd_get_next_file(self, dd: Dict):
        dent = self._dd_get_next_file_dent(dd)
        if not dent:
            return (None, None)
        return (dent.name, dent.path)

    # reported_to handling

    def libreport_add_reported_to(self, dd: Dict, line: str):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        reported_to = self.dd_load_text_ext(dd,
                                            const.FILENAME_REPORTED_TO,
                                            DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE)
        reported_to, altered = self.reported_to.libreport_add_reported_to_data(reported_to, line)
        if altered:
            self.dd_save_text(dd, const.FILENAME_REPORTED_TO, reported_to)

    def libreport_add_reported_to_entry(self, dd: Dict, result: Dict):
        if not dd['locked']:
            self.logger.error("dump_dir is not opened")
            sys.exit(1)  # bug

        reported_to = self.dd_load_text_ext(dd,
                                            const.FILENAME_REPORTED_TO,
                                            DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE)
        reported_to, altered = self.reported_to.libreport_add_reported_to_entry_data(reported_to, result)
        if altered:
            self.dd_save_text(dd, const.FILENAME_REPORTED_TO, reported_to)

    def libreport_find_in_reported_to(self, dd: Dict, report_label: str):
        reported_to = self.dd_load_text_ext(dd,
                                            const.FILENAME_REPORTED_TO,
                                            DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE)
        if not reported_to:
            return None

        result = self.reported_to.libreport_find_in_reported_to_data(reported_to, report_label)

        return result

    def libreport_read_entire_reported_to(self, dd: Dict):
        reported_to = self.dd_load_text_ext(dd,
                                            const.FILENAME_REPORTED_TO,
                                            DD_FAIL_QUIETLY_ENOENT | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE)
        if not reported_to:
            return None

        result = self.reported_to.libreport_read_entire_reported_to_data(reported_to)

        return result

    # reported_to handling end

    def dd_create_archive(self, dd: Dict, archive_name: str, exclude_elements: List[str]):
        if archive_name.endswith('.tar'):
            mode = 'x'
        if archive_name.endswith('.tar.gz'):
            mode = 'x:gz'
        elif archive_name.endswith('.tar.bz2'):
            mode = 'x:bz2'
        elif archive_name.endswith('.tar.xz'):
            mode = 'x:xz'
        else:
            raise NotImplementedError

        # in case None is passed
        if not exclude_elements:
            exclude_elements = []

        result = 0

        # TODO: Find a way to modify permissions of files in archive, if needed

        try:
            with tarfile.open(archive_name, mode) as archive:

                # Write data to the tarball
                self.dd_init_next_file(dd)

                while True:
                    (short_name, _) = self.dd_get_next_file(dd)
                    if not short_name:
                        break
                    if short_name in exclude_elements:
                        continue
                    archive.add(short_name)
        except OSError as exc:
            result = -exc.errno

        return result
