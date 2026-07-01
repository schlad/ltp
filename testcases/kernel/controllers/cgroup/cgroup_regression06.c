// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Exercise adding and removing the cpu controller by remounting a cgroup v1
 * hierarchy after removing a child directory that is still held open.
 * Changing controllers while a removed cgroup remained alive used to leak
 * memory and could cause an oops when reading scheduler debug information.
 *
 * Each case first verifies that the controller change works without a child.
 * With the removed directory held open, remount may return EBUSY or succeed
 * if the kernel has already released the cgroup independently of the dentry.
 * Check the resulting controller state, then drop caches and read scheduler
 * debug information to exercise the affected cleanup path.
 *
 * Root is required to mount cgroup v1, drop caches and read scheduler debug
 * information. The cpu controller must be available for a private hierarchy.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT "cgroup"
#define SUBDIR MNTPOINT "/0"

static struct tcase {
	const char *desc;
	int initial_cpu;
	int final_cpu;
} tcases[] = {
	{"add cpu", 0, 1},
	{"remove cpu", 1, 0},
};

static int mounted, subdir_created;
static int dir_fd = -1, sched_fd = -1;
static const char *sched_debug;
static char cpu_opts[128], empty_opts[128];

static void unmount_hierarchy(void)
{
	if (tst_umount(MNTPOINT))
		tst_brk(TBROK, "Failed to unmount cgroup hierarchy");
	mounted = 0;
}

static void run(unsigned int n)
{
	struct tcase *tc = &tcases[n];
	char buf[4096];
	int expected_cpu, actual_cpu, failed = 0;

	snprintf(cpu_opts, sizeof(cpu_opts), "cpu,name=ltp_reg06_%ld_%u",
		 (long)getpid(), n);
	snprintf(empty_opts, sizeof(empty_opts), "none,name=ltp_reg06_%ld_%u",
		 (long)getpid(), n);

	TEST(mount("cgroup", MNTPOINT, "cgroup", 0, cpu_opts));
	if (TST_RET) {
		if (TST_ERR == EBUSY || TST_ERR == ENODEV ||
		    TST_ERR == ENOENT || TST_ERR == EINVAL) {
			tst_res(TCONF | TTERRNO, "cpu unavailable for a private v1 hierarchy");
			return;
		}
		tst_brk(TBROK | TTERRNO, "Failed to mount cpu hierarchy");
	}
	mounted = 1;

	/* Prove both controller transitions work before creating a child. */
	SAFE_MOUNT("cgroup", MNTPOINT, "cgroup", MS_REMOUNT, empty_opts);
	SAFE_MOUNT("cgroup", MNTPOINT, "cgroup", MS_REMOUNT, cpu_opts);
	if (!tc->initial_cpu)
		SAFE_MOUNT("cgroup", MNTPOINT, "cgroup", MS_REMOUNT, empty_opts);

	SAFE_MKDIR(SUBDIR, 0755);
	subdir_created = 1;
	dir_fd = SAFE_OPEN(SUBDIR, O_RDONLY | O_DIRECTORY);
	SAFE_RMDIR(SUBDIR);
	subdir_created = 0;

	TEST(mount("cgroup", MNTPOINT, "cgroup", MS_REMOUNT,
		   tc->final_cpu ? cpu_opts : empty_opts));
	if (TST_RET && TST_ERR != EBUSY) {
		tst_res(TFAIL | TTERRNO, "%s remount failed unexpectedly", tc->desc);
		failed = 1;
	} else {
		expected_cpu = TST_RET ? tc->initial_cpu : tc->final_cpu;
		tst_res(TINFO, "%s remount %s", tc->desc,
			TST_RET ? "rejected with EBUSY" : "succeeded");
		actual_cpu = tst_mount_has_opt(MNTPOINT, "cpu");
		if (actual_cpu != expected_cpu) {
			tst_res(TFAIL, "%s: cpu attachment is %d, expected %d",
				tc->desc, actual_cpu, expected_cpu);
			failed = 1;
		}
	}

	SAFE_CLOSE(dir_fd);
	unmount_hierarchy();

	for (int i = 0; i < 50; i++) {
		SAFE_FILE_PRINTF("/proc/sys/vm/drop_caches", "3");
		sched_fd = SAFE_OPEN(sched_debug, O_RDONLY);
		while (SAFE_READ(0, sched_fd, buf, sizeof(buf)) > 0)
			;
		SAFE_CLOSE(sched_fd);
	}

	if (!failed)
		tst_res(TPASS, "%s with a removed directory held open", tc->desc);
}

static void setup(void)
{
	const char *const paths[] = {
		"/proc/sched_debug",
		"/sys/kernel/debug/sched/debug",
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(paths); i++) {
		if (!access(paths[i], R_OK)) {
			sched_debug = paths[i];
			break;
		}
	}
	if (!sched_debug)
		tst_brk(TCONF, "scheduler debug file not available");

	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (sched_fd != -1)
		SAFE_CLOSE(sched_fd);
	if (dir_fd != -1)
		SAFE_CLOSE(dir_fd);
	if (subdir_created)
		SAFE_RMDIR(SUBDIR);
	if (mounted)
		unmount_hierarchy();
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.tcnt = ARRAY_SIZE(tcases),
	.timeout = 60,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "307257cf475aac25db30b669987f13d90c934e3a"},
		{}
	},
};
