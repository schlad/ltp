// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for notify_on_release flag not being inherited from
 * parent to child cgroup in a cgroup v1 named hierarchy.
 *
 * Kernel: 2.6.24-rcX
 * Fixed by: commit bc231d2a048010d5e0b49ac7fddbfa822fc41109
 *
 * References:
 * - http://lkml.org/lkml/2008/2/25/12
 */

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define CHILD0		MNTPOINT "/child0"
#define CHILD1		MNTPOINT "/child1"

static int mounted;

static void run(void)
{
	int val0, val1;

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression02")) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	SAFE_FILE_PRINTF(MNTPOINT "/notify_on_release", "0");
	SAFE_MKDIR(CHILD0, 0755);
	SAFE_FILE_SCANF(CHILD0 "/notify_on_release", "%d", &val0);

	SAFE_FILE_PRINTF(MNTPOINT "/notify_on_release", "1");
	SAFE_MKDIR(CHILD1, 0755);
	SAFE_FILE_SCANF(CHILD1 "/notify_on_release", "%d", &val1);

	TST_EXP_EQ_LI(val0, 0);
	TST_EXP_EQ_LI(val1, 1);

	SAFE_RMDIR(CHILD0);
	SAFE_RMDIR(CHILD1);
	tst_umount(MNTPOINT);
	mounted = 0;
}

static void setup(void)
{
	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	rmdir(CHILD0);
	rmdir(CHILD1);

	if (mounted)
		tst_umount(MNTPOINT);
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.timeout = 30,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "bc231d2a0480"},
		{}
	},
};
