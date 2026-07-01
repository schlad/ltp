// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for memory leak (and potential oops) when remounting a
 * cgroup filesystem that has dead subdirectories (rmdir'd but still held
 * open by a file descriptor). Also exercises /proc/sched_debug read after
 * such a remount sequence.
 *
 * Kernel: 2.6.24 - 2.6.27, 2.6.28-rcX
 * Fixed by: commit 307257cf475aac25db30b669987f13d90c934e3a
 *
 * References:
 * - http://lkml.org/lkml/2008/12/10/369
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define SUBDIR		MNTPOINT "/0"

static int mounted;
static pid_t holder = -1;

/*
 * Hold a directory fd open to keep a refcount on a cgroup subdir after rmdir.
 * The parent signals us with SIGTERM when done.
 */
static void hold_dir_open(void)
{
	int fd;

	fd = open(SUBDIR, O_RDONLY);
	if (fd < 0)
		exit(1);
	pause();
	close(fd);
	exit(0);
}

static void run(unsigned int n)
{
	const char *opts = (n == 0)
		? "none,name=cgroup_regression07a"
		: "none,name=cgroup_regression07b";

	if (mount("cgroup", MNTPOINT, "cgroup", 0, opts)) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	SAFE_MKDIR(SUBDIR, 0755);

	holder = SAFE_FORK();
	if (holder == 0)
		hold_dir_open();

	usleep(50000);

	/* rmdir succeeds but dentry stays alive due to the open fd */
	rmdir(SUBDIR);

	/* remount rejected since 2.6.28 when dead subdirs exist */
	mount("cgroup", MNTPOINT, "cgroup", MS_REMOUNT, opts);

	kill(holder, SIGTERM);
	waitpid(holder, NULL, 0);
	holder = -1;

	tst_umount(MNTPOINT);
	mounted = 0;

	if (n == 0) {
		tst_res(TPASS, "subtest 1: no kernel bug was found");
		return;
	}

	if (access("/proc/sched_debug", F_OK)) {
		tst_res(TCONF, "subtest 2: skipping sched_debug stress (CONFIG_SCHED_DEBUG not set)");
		return;
	}

	for (int i = 0; i < 50; i++) {
		int fd;
		char buf[4096];

		SAFE_FILE_PRINTF("/proc/sys/vm/drop_caches", "3");

		fd = open("/proc/sched_debug", O_RDONLY);
		if (fd < 0)
			continue;
		while (read(fd, buf, sizeof(buf)) > 0)
			;
		close(fd);
	}

	tst_res(TPASS, "subtest 2: no kernel bug was found");
}

static void setup(void)
{
	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (holder > 0) {
		kill(holder, SIGTERM);
		waitpid(holder, NULL, 0);
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
	.tcnt = 2,
	.timeout = 60,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "307257cf475a"},
		{}
	},
};
