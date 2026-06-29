// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for cgroup hierarchy lock's lockdep subclass overflow.
 * With more than MAX_LOCKDEP_SUBCLASSES (8) cgroup subsystems mounted,
 * creating and removing a cgroup would trigger a lockdep splat.
 *
 * Kernel: 2.6.29-rcX
 *
 * References:
 * - http://lkml.org/lkml/2009/2/4/67
 */

#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define SUBDIR		MNTPOINT "/0"

static int mounted;

static void run(void)
{
	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression04")) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	SAFE_MKDIR(SUBDIR, 0755);
	SAFE_RMDIR(SUBDIR);

	tst_umount(MNTPOINT);
	mounted = 0;

	tst_res(TPASS, "no lockdep BUG was found");
}

static void setup(void)
{
	FILE *fp;
	int lines = 0;
	char buf[256];

	if (access("/proc/lockdep", F_OK))
		tst_brk(TCONF, "CONFIG_LOCKDEP is not enabled");

	fp = fopen("/proc/cgroups", "r");
	if (!fp)
		tst_brk(TBROK | TERRNO, "Failed to open /proc/cgroups");
	while (fgets(buf, sizeof(buf), fp))
		lines++;
	fclose(fp);

	/* header + 8 subsystems = 9 lines; need more than 8 subsystems */
	if (lines <= 9)
		tst_brk(TCONF, "requires more than 8 cgroup subsystems, got %d", lines - 1);

	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	rmdir(SUBDIR);
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
};
