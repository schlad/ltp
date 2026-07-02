// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Test that re-mounting a cgroup v1 named hierarchy immediately after umount
 * does not return EBUSY.
 *
 * The test mounts and immediately re-mounts a named hierarchy 1000 times.
 * Any EBUSY on re-mount is a failure.
 */

#include <errno.h>
#include <sys/mount.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define MOUNT_OPTS	"none,name=cgroup_regression02"
#define ITERATIONS	1000

static int mounted;

static void run(void)
{
	int i;

	if (mount("cgroup", MNTPOINT, "cgroup", 0, MOUNT_OPTS)) {
		if (errno == ENODEV || errno == ENOENT)
			tst_brk(TCONF, "cgroup named hierarchy not available");
		tst_brk(TBROK | TERRNO, "initial mount failed");
	}
	mounted = 1;

	for (i = 0; i < ITERATIONS; i++) {
		if (i % 25 == 0)
			tst_res(TINFO, "iteration %d/%d", i, ITERATIONS);

		if (tst_umount(MNTPOINT))
			tst_brk(TBROK | TERRNO, "umount failed");
		mounted = 0;

		TEST(mount("cgroup", MNTPOINT, "cgroup", 0, MOUNT_OPTS));
		if (TST_RET) {
			if (TST_ERR == EBUSY)
				tst_brk(TFAIL | TTERRNO,
					"EBUSY on re-mount after umount (iteration %d)", i);
			tst_brk(TBROK | TTERRNO, "re-mount failed on iteration %d", i);
		}
		mounted = 1;
	}

	if (tst_umount(MNTPOINT))
		tst_brk(TBROK | TERRNO, "final umount failed");
	mounted = 0;

	tst_res(TPASS, "completed %d iterations without EBUSY on re-mount", ITERATIONS);
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
	.timeout = 60,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
};
