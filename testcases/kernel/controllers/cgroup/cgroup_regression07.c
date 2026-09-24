// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for oops when calling cgroupstat on a cgroup control
 * file (tasks). The cgroupstats Netlink handler was not checking that the target
 * is a directory, and would dereference a bad pointer on a regular file.
 *
 * Root is required to mount and unmount cgroup v1 hierarchies.
 *
 * Kernel: 2.6.24 - 2.6.27, 2.6.28-rcX
 *
 * References:
 *
 * - http://lkml.org/lkml/2008/11/19/53
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>
#include "tst_test.h"
#include "cgroup_regression_getdelays.h"

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
	int status;
	const char *const argv[] = {
		"cgroup_regression_getdelays", "-C", TASKS_FILE, NULL
	};

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression07")) {
		if (errno == ENODEV || errno == ENOENT)
			tst_brk(TCONF | TERRNO, "cgroup v1 named hierarchy not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	status = tst_cmd(argv, NULL, NULL,
		TST_CMD_PASS_RETVAL | TST_CMD_TCONF_ON_MISSING);

	switch (status) {
	case CGROUPSTATS_UNAVAILABLE:
		tst_brk(TCONF, "cgroupstats support unavailable");
		break;
	case 0:
		tst_res(TFAIL, "getdelays should have failed on a cgroup tasks file");
		break;
	case CGROUPSTATS_REJECTED:
		tst_res(TPASS, "cgroupstats rejected the tasks file with EINVAL");
		break;
	default:
		tst_brk(TBROK, "getdelays failed with status %d", status);
	}

	tst_umount(MNTPOINT);
	mounted = 0;
}

static void setup(void)
{
	prepend_self_dir_to_path();
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
		{"linux-git", "33d283bef23132c48195eafc21449f8ba88fce6b"},
		{}
	},
};
