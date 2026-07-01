// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for oops when calling cgroupstat on a cgroup control
 * file (tasks). The cgroupstat ioctl was not checking that the target
 * is a directory, and would dereference a bad pointer on a regular file.
 *
 * Kernel: 2.6.24 - 2.6.27, 2.6.28-rcX
 * Fixed by: commit 33d283bef23132c48195eafc21449f8ba88fce6b
 *
 * References:
 * - http://lkml.org/lkml/2008/11/19/53
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define TASKS_FILE	MNTPOINT "/tasks"

static int mounted;

static void prepend_self_dir_to_path(void)
{
	char self[PATH_MAX], newpath[PATH_MAX * 2];
	ssize_t n;
	char *sep;

	n = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (n <= 0)
		return;
	self[n] = '\0';

	sep = strrchr(self, '/');
	if (!sep)
		return;
	*sep = '\0';

	snprintf(newpath, sizeof(newpath), "%s:%s", self, getenv("PATH") ?: "");
	setenv("PATH", newpath, 1);
}

static void run(void)
{
	pid_t pid;
	int status, null_fd;
	char *const argv[] = {
		"cgroup_regression_getdelays", "-C", TASKS_FILE, NULL
	};

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression08")) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	pid = fork();
	if (pid < 0)
		tst_brk(TBROK | TERRNO, "fork failed");

	if (pid == 0) {
		null_fd = open("/dev/null", O_WRONLY);
		if (null_fd >= 0) {
			dup2(null_fd, STDOUT_FILENO);
			dup2(null_fd, STDERR_FILENO);
			close(null_fd);
		}
		prepend_self_dir_to_path();
		execvp(argv[0], argv);
		exit(errno == ENOENT ? 77 : 1);
	}

	if (waitpid(pid, &status, 0) < 0)
		tst_brk(TBROK | TERRNO, "waitpid failed");

	if (!WIFEXITED(status))
		tst_brk(TBROK, "getdelays killed by signal %d", WTERMSIG(status));

	switch (WEXITSTATUS(status)) {
	case 77:
		tst_brk(TCONF, "cgroup_regression_getdelays binary not found");
		break;
	case 0:
		tst_res(TFAIL, "getdelays should have failed on a cgroup tasks file");
		break;
	default:
		tst_res(TPASS, "no kernel bug was found");
		break;
	}

	tst_umount(MNTPOINT);
	mounted = 0;
}

static void setup(void)
{
	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (mounted)
		tst_umount(MNTPOINT);
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.timeout = 30,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "33d283bef231"},
		{}
	},
};
