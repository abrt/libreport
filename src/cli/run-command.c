/*
    Copyright (C) 2009  RedHat inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "internal_libreport.h"
#include "run-command.h"

/*
  Inspired by git code.
  http://git.kernel.org/?p=git/git.git;a=blob;f=run-command.c;hb=HEAD
*/

struct command {
    char **argv;
    pid_t pid;
    int tty_fd;
    pid_t bck_tcgrp;            //<< old foreground process group
    struct termios bck_tmodes;  //<< old configuration
};

static void start_command(struct command *cmd)
{
  cmd->tty_fd = open("/dev/tty", O_RDWR);
  if (cmd->tty_fd < 0)
    /* do we want to die if we're in a script and have no controlling tty?
     *
     * YES, they can always call 'report-cli -y' from script and it should
     * not try to open an editor
     */
    perror_msg_and_die("open(\"/dev/tty\", O_RDWR)");

  /* save current foreground process group id */
  cmd->bck_tcgrp = tcgetpgrp(cmd->tty_fd);
  if (cmd->bck_tcgrp < 0)
    perror_msg_and_die("tcgetpgrp()");

  /* save tty's attrs */
  if (tcgetattr(cmd->tty_fd, &cmd->bck_tmodes) < 0)
    perror_msg_and_die("tcsetattr()");

  fflush(NULL);

  cmd->pid = vfork();
  if (cmd->pid < 0)
  {
    perror_msg_and_die("vfork");
  }
  if (cmd->pid == 0)
  {
    /* Child */
    /* Don't close tty_fd, will be used for changing of foreground group later */
    xdup2(cmd->tty_fd, 0);
    xdup2(cmd->tty_fd, 1);
    xdup2(cmd->tty_fd, 2);

    /* tcsetpgrp() below will send us SIGTTOU if we aren't
     * foreground process group. Need to ignore it */
    signal(SIGTTOU, SIG_IGN);

    /* sends SIGTTOU to process group */
    pid_t pgrp = getpgrp();
    if (pgrp < 0) {
      perror_msg("getpgrp()");
      _exit(127);
    }
    if (tcsetpgrp(cmd->tty_fd, pgrp) < 0) {
      perror_msg("tcsetpgrp()");
      _exit(127);
    }

    signal(SIGTTOU, SIG_DFL);

    /* became useless */
    close(cmd->tty_fd);

    execvp(cmd->argv[0], cmd->argv);
    /* Better to use _exit (not exit) after vfork:
     * we don't want to mess up parent's memory state
     * by running libc cleanup routines.
     */
    _exit(127);
  }
}

static int finish_command(struct command *cmd)
{
  int status;
  pid_t waiting = safe_waitpid(cmd->pid, &status, 0);
  if (waiting < 0)
    perror_msg_and_die("waitpid");

  /* tcsetpgrp() below will send us SIGTTOU if we aren't
   * foreground process group. Need to ignore it */
  sighandler_t old = signal(SIGTTOU, SIG_IGN);

  /* reset foreground process group */
  if (tcsetpgrp(cmd->tty_fd, cmd->bck_tcgrp) < 0)
      perror_msg_and_die("tcsetpgrp()");
  /* restore old tty attrs (not really needed, but just in case editor mangled them) */
  if (tcsetattr(cmd->tty_fd, TCSADRAIN, &(cmd->bck_tmodes)) < 0)
      perror_msg_and_die("tcsetattr()");

  signal(SIGTTOU, old);

  close(cmd->tty_fd);

  int code;
  if (WIFSIGNALED(status))
  {
    code = WTERMSIG(status);
    error_msg("'%s' killed by signal %d", cmd->argv[0], code);
    code += 128; /* shells use this convention for deaths by signal */
  }
  else /* if (WIFEXITED(status)) */
  {
    code = WEXITSTATUS(status);
    if (code == 127)
    {
      error_msg_and_die("Can't run '%s'", cmd->argv[0]);
    }
  }

  return code;
}

int run_command(char **argv)
{
  struct command cmd = { argv, 0 };
  start_command(&cmd);
  return finish_command(&cmd);
}
