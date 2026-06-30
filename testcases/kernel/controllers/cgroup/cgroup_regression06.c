// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for false positive lockdep warning triggered by two
 * concurrent threads doing cgroup mount/umount. One thread also reads
 * the release_agent file during the cycle. The bug is in VFS, not cgroup.
 *
 * Kernel: 2.6.24 - 2.6.29-rcX
 * Fixed by: commit ada723dcd681e2dffd7d73345cc8fda0eb0df9bd
 *
 * References:
 * - http://lkml.org/lkml/2009/1/4/352
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT		"cgroup"
#define RELEASE_AGENT		MNTPOINT "/release_agent"

static pid_t pid1 = -1, pid2 = -1;

static void loop_mount_read_umount(void)
{
	int fd;
	char buf[256];

	for (;;) {
		if (!mount("none", MNTPOINT, "cgroup", 0, "")) {
			fd = open(RELEASE_AGENT, O_RDONLY);
			if (fd >= 0) {
				while (read(fd, buf, sizeof(buf)) > 0)
					;
				close(fd);
			}
			umount(MNTPOINT);
		}
	}
}

static void loop_mount_umount(void)
{
	for (;;) {
		mount("none", MNTPOINT, "cgroup", 0, "");
		umount(MNTPOINT);
	}
}

static void run(void)
{
	pid1 = SAFE_FORK();
	if (pid1 == 0)
		loop_mount_read_umount();

	pid2 = SAFE_FORK();
	if (pid2 == 0)
		loop_mount_umount();

	sleep(30);

	kill(pid1, SIGTERM);
	kill(pid2, SIGTERM);
	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);
	pid1 = pid2 = -1;

	tst_res(TPASS, "no kernel bug was found");
}

static void setup(void)
{
	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (pid1 > 0) {
		kill(pid1, SIGTERM);
		waitpid(pid1, NULL, 0);
	}
	if (pid2 > 0) {
		kill(pid2, SIGTERM);
		waitpid(pid2, NULL, 0);
	}
	/* children may have left an active mount if killed mid-cycle */
	umount(MNTPOINT);
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.forks_child = 1,
	.timeout = 60,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "ada723dcd681"},
		{}
	},
};
