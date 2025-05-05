/*
 * sisyphus.c: Utility to restart a command in a loop.
 * Copyright (C) 2025 Calvin Owens <calvin@wbinvd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <sys/wait.h>

static sig_atomic_t stop;

static void stopper(int sig)
{
	stop = sig;
}

static void run_child(char *argv[], char *envp[])
{
	execvpe(argv[0], argv, envp);
	warn("Failed to execute child");
}

int main(int argc, char *argv[], char *envp[])
{
	struct sigaction stop_action = {
		.sa_handler = stopper,
	};

	sigaction(SIGINT, &stop_action, NULL);
	sigaction(SIGTERM, &stop_action, NULL);

	if (argc < 2)
		errx(1, "Usage: ./sisyphus <executable> [arg1 [arg2...]]");

	while (!stop) {
		int wstatus;
		pid_t ret;

		ret = fork();
		if (ret == -1) {
			err(1, "Can't fork child");
		} else if (ret == 0) {
			run_child(argv + 1, envp);
			_exit(1);
		}
again:
		if (waitpid(ret, &wstatus, 0) != ret) {
			if (errno == EINTR) {
				if (stop && kill(ret, SIGTERM))
					warn("Can't signal child");

				goto again;
			}

			err(1, "Can't wait for child");
		}

		if (WIFSTOPPED(wstatus) || WIFCONTINUED(wstatus))
			goto again;

		if (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus))
			errx(1, "Bad status (%08x)", (unsigned)wstatus);

		usleep(100 * 1000);
	}

	return 0;
}
