// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for EBUSY on re-mounting a cgroup v1 named hierarchy
 * immediately after umount. The named hierarchy teardown is asynchronous
 * (percpu_ref_kill -> RCU -> workqueue -> cgroup_destroy_root), so the
 * name may still be reserved when umount(2) returns to userspace.
 *
 * The test mounts and umounts a named hierarchy 1000 times, re-mounting
 * immediately after each umount. Up to 3 consecutive EBUSY results on
 * re-mount are tolerated per iteration; more than that is a failure.
 */

#include <errno.h>
#include <sys/mount.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define MOUNT_OPTS	"none,name=cgroup_regression09"
#define ITERATIONS	1000
#define MAX_RETRIES	3

static int mounted;

static void run(void)
{
	int i, attempt, total_ebusy = 0;

	for (i = 0; i < ITERATIONS; i++) {
		if (i % 25 == 0)
			tst_res(TINFO, "iteration %d/%d (EBUSY hits so far: %d)",
				i, ITERATIONS, total_ebusy);

		if (mount("cgroup", MNTPOINT, "cgroup", 0, MOUNT_OPTS)) {
			if (errno == ENODEV)
				tst_brk(TCONF, "cgroup filesystem not available");
			tst_brk(TBROK | TERRNO, "mount failed on iteration %d", i);
		}
		mounted = 1;

		tst_umount(MNTPOINT);
		mounted = 0;

		for (attempt = 0; attempt < MAX_RETRIES; attempt++) {
			if (!mount("cgroup", MNTPOINT, "cgroup", 0, MOUNT_OPTS))
				break;
			if (errno != EBUSY)
				tst_brk(TBROK | TERRNO, "unexpected error on re-mount (iteration %d)", i);
			total_ebusy++;
			tst_res(TINFO, "iteration %d: EBUSY on re-mount attempt %d/%d",
				i, attempt + 1, MAX_RETRIES);
		}

		if (attempt == MAX_RETRIES) {
			tst_res(TFAIL,
				"EBUSY persisted across all %d re-mount attempts after umount (iteration %d)",
				MAX_RETRIES, i);
			return;
		}

		mounted = 1;
		tst_umount(MNTPOINT);
		mounted = 0;
	}

	tst_res(TPASS, "completed %d iterations, %d EBUSY events, all resolved within %d retries",
		ITERATIONS, total_ebusy, MAX_RETRIES);
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
