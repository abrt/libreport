/*
 * Utility routines.
 *
 * Copyright (C) 2010  ABRT team
 * Copyright (C) 2010  RedHat Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <glib/gstdio.h>
#include "internal_libreport.h"

static char *concat_str_vector(char **strings)
{
	if (!strings[0])
		return g_malloc0(1); // returns ""

	unsigned len = 0;
	char **spp = strings;
	while (*spp)
		len += strlen(*spp++) + 1;

	char *result = g_malloc(len);

	char *r = result;
	spp = strings;
	while (*spp) {
		r = stpcpy(r, *spp++);
		*r++ = ' ';
	}
	*--r = '\0';

	return result;
}

/* Returns pid */
pid_t libreport_fork_execv_on_steroids(int flags,
		char **argv,
		int *pipefds,
		char **env_vec,
		const char *dir,
		uid_t uid)
{
	pid_t child;
	/* Reminder: [0] is read end, [1] is write end */
	int pipe_to_child[2];
	int pipe_fm_child[2];

	/* Sanitize flags */
	if (!pipefds)
		flags &= ~(EXECFLG_INPUT | EXECFLG_OUTPUT);

	if (flags & EXECFLG_INPUT)
		libreport_xpipe(pipe_to_child);
	if (flags & EXECFLG_OUTPUT)
		libreport_xpipe(pipe_fm_child);

	/* Prepare it before fork, to avoid thread-unsafe malloc there */
	char *prog_as_string = NULL;
	prog_as_string = concat_str_vector(argv);
	gid_t gid;
	if (flags & EXECFLG_SETGUID) {
		struct passwd* pw = getpwuid(uid);
		gid = pw ? pw->pw_gid : uid;
	}

	fflush(NULL);
	child = fork();
	if (child == -1) {
		perror_msg_and_die("fork");
	}
	if (child == 0) {
		/* Child */

		if (dir)
			g_chdir(dir);

		if (flags & EXECFLG_SETGUID) {
			setgroups(1, &gid);
			libreport_xsetregid(gid, gid);
			libreport_xsetreuid(uid, uid);
		}

		if (env_vec) {
			/* Note: we use the glibc extension that putenv("var")
			 * *unsets* $var if "var" string has no '=' */
			while (*env_vec)
				putenv(*env_vec++);
		}

		/* Play with stdio descriptors */
		if (flags & EXECFLG_INPUT) {
			/* NB: close must be first, because
			 * pipe_to_child[1] may be equal to STDIN_FILENO
			 */
			close(pipe_to_child[1]);
			libreport_xmove_fd(pipe_to_child[0], STDIN_FILENO);
		} else if (flags & EXECFLG_INPUT_NUL) {
			libreport_xmove_fd(g_open("/dev/null", O_RDWR), STDIN_FILENO);
		}
		if (flags & EXECFLG_OUTPUT) {
			close(pipe_fm_child[0]);
			libreport_xmove_fd(pipe_fm_child[1], STDOUT_FILENO);
		} else if (flags & EXECFLG_OUTPUT_NUL) {
			libreport_xmove_fd(g_open("/dev/null", O_RDWR), STDOUT_FILENO);
		}

		/* This should be done BEFORE stderr redirect */
		log_info("Executing: %s", prog_as_string);

		if (flags & EXECFLG_ERR2OUT) {
			/* Want parent to see errors in the same stream */
			libreport_xdup2(STDOUT_FILENO, STDERR_FILENO);
		} else if (flags & EXECFLG_ERR_NUL) {
			libreport_xmove_fd(g_open("/dev/null", O_RDWR), STDERR_FILENO);
		}

		if (flags & EXECFLG_SETSID)
			setsid();
		if (flags & EXECFLG_SETPGID)
			setpgid(0, 0);

		execvp(argv[0], argv);
		if (!(flags & EXECFLG_QUIET))
			perror_msg("Can't execute '%s'", argv[0]);
		_exit(127); /* shell uses this exit code in this case */
	}

	/* Parent */
	free(prog_as_string);

	if (flags & EXECFLG_INPUT) {
		close(pipe_to_child[0]);
		pipefds[1] = pipe_to_child[1];
	}
	if (flags & EXECFLG_OUTPUT) {
		close(pipe_fm_child[1]);
		pipefds[0] = pipe_fm_child[0];
	}

	return child;
}

char *libreport_run_in_shell_and_save_output(int flags,
		const char *cmd,
		const char *dir,
		size_t *size_p)
{
	flags |= EXECFLG_OUTPUT;
	flags &= ~EXECFLG_INPUT;

	const char *argv[] = { "/bin/sh", "-c", cmd, NULL };
	int pipeout[2];
	pid_t child = libreport_fork_execv_on_steroids(flags, (char **)argv, pipeout,
		/*env_vec:*/ NULL, dir, /*uid (unused):*/ 0);

	size_t pos = 0;
	char *result = NULL;
	while (1) {
		result = (char*) g_realloc(result, pos + 4*1024 + 1);
		size_t sz = libreport_safe_read(pipeout[0], result + pos, 4*1024);
		if (sz <= 0) {
			break;
		}
		pos += sz;
	}
	result[pos] = '\0';
	if (size_p)
		*size_p = pos;
	close(pipeout[0]);
	libreport_safe_waitpid(child, NULL, 0);

	return result;
}

pid_t libreport_safe_waitpid(pid_t pid, int *wstat, int options)
{
	pid_t r;

	do
		r = waitpid(pid, wstat, options);
	while ((r == -1) && (errno == EINTR));
	return r;
}
