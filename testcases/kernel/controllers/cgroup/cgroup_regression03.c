// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for NULL cgrp->dentry access when reading /proc/sched_debug
 * while concurrently creating and removing cgroup subdirectories.
 *
 * Kernel: 2.6.26-2.6.28
 * Fixed by: commit a47295e6bc42ad35f9c15ac66f598aa24debd4e2
 *
 * References:
 * - http://lkml.org/lkml/2008/10/30/44
 * - http://lkml.org/lkml/2008/12/12/107
 * - http://lkml.org/lkml/2008/12/16/481
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define SUBDIR		MNTPOINT "/subdir"

static int mounted;
static pid_t pid1 = -1, pid2 = -1;

static void mkdir_rmdir_loop(void)
{
	for (;;) {
		mkdir(SUBDIR, 0755);
		rmdir(SUBDIR);
	}
}

static void read_sched_debug_loop(void)
{
	int fd;
	char buf[4096];

	for (;;) {
		fd = open("/proc/sched_debug", O_RDONLY);
		if (fd < 0)
			continue;
		while (read(fd, buf, sizeof(buf)) > 0)
			;
		close(fd);
	}
}

static void run(void)
{
	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression03")) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	pid1 = SAFE_FORK();
	if (pid1 == 0)
		mkdir_rmdir_loop();

	pid2 = SAFE_FORK();
	if (pid2 == 0)
		read_sched_debug_loop();

	sleep(30);

	kill(pid1, SIGTERM);
	kill(pid2, SIGTERM);
	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);
	pid1 = pid2 = -1;

	rmdir(SUBDIR);
	tst_umount(MNTPOINT);
	mounted = 0;

	tst_res(TPASS, "no kernel bug was found");
}

static void setup(void)
{
	if (access("/proc/sched_debug", F_OK))
		tst_brk(TCONF, "CONFIG_SCHED_DEBUG is not enabled");

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
	if (mounted) {
		rmdir(SUBDIR);
		tst_umount(MNTPOINT);
	}
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
		{"linux-git", "a47295e6bc42"},
		{}
	},
};
