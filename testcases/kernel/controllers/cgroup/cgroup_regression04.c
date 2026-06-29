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
 * Lockdep reports do not taint the kernel, so the test checks that lockdep
 * is still enabled (debug_locks in /proc/lockdep_stats) after the operations.
 *
 * Root is required to mount and unmount cgroup v1 hierarchies.
 * The test requires more than eight enabled controllers that can be mounted
 * together in one v1 hierarchy.
 *
 * Kernel: 2.6.29-rcX
 *
 * References:
 *
 * - http://lkml.org/lkml/2009/2/4/67
 */

#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>
#include "tst_test.h"
#include "tst_safe_stdio.h"

#define MNTPOINT	"cgroup"
#define SUBDIR		MNTPOINT "/0"
#define LOCKDEP_STATS	"/proc/lockdep_stats"

static int mounted;
static int subdir_created;
static FILE *cgroups;
static char mount_opts[1024];

/*
 * Lockdep reports do not taint the kernel, but lockdep turns itself off
 * after the first report by clearing debug_locks.
 *
 * Returns -1 when lockdep is not available, otherwise the debug_locks value.
 */
static int lockdep_debug_locks(void)
{
	int val;

	if (access(LOCKDEP_STATS, F_OK))
		return -1;

	SAFE_FILE_LINES_SCANF(LOCKDEP_STATS, " debug_locks: %d", &val);

	return val;
}

static void run(void)
{
	TEST(mount("cgroup", MNTPOINT, "cgroup", 0, mount_opts));
	if (TST_RET) {
		/* cgroup_no_v1 can leave controllers listed in /proc/cgroups. */
		if (TST_ERR == ENODEV || TST_ERR == ENOENT || TST_ERR == EINVAL)
			tst_brk(TCONF | TTERRNO, "cgroup v1 controllers not available");
		if (TST_ERR == EBUSY)
			tst_brk(TCONF, "Controllers cannot be mounted together in one hierarchy");
		tst_brk(TBROK | TTERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	SAFE_MKDIR(SUBDIR, 0755);
	subdir_created = 1;
	SAFE_RMDIR(SUBDIR);
	subdir_created = 0;

	if (tst_umount(MNTPOINT))
		tst_brk(TBROK, "Failed to unmount cgroup filesystem");
	mounted = 0;

	if (!lockdep_debug_locks()) {
		tst_res(TFAIL, "lockdep reported a problem, see dmesg");
		return;
	}

	tst_res(TPASS, "no lockdep BUG was found");
}

static void setup(void)
{
	int controllers = 0, lockdep, enabled, len;
	size_t used = 0;
	char buf[256], name[64];

	lockdep = lockdep_debug_locks();
	if (lockdep < 0)
		tst_brk(TCONF, "CONFIG_LOCKDEP is not enabled");
	if (!lockdep)
		tst_brk(TCONF, "lockdep already disabled by an earlier report");

	cgroups = SAFE_FOPEN("/proc/cgroups", "r");
	while (fgets(buf, sizeof(buf), cgroups)) {
		if (buf[0] == '#')
			continue;
		if (sscanf(buf, "%63s %*u %*u %d", name, &enabled) != 2)
			tst_brk(TBROK, "Failed to parse /proc/cgroups: %s", buf);
		if (!enabled)
			continue;

		len = snprintf(mount_opts + used, sizeof(mount_opts) - used,
			       "%s%s", controllers ? "," : "", name);
		if (len < 0 || (size_t)len >= sizeof(mount_opts) - used)
			tst_brk(TBROK, "Controller mount options too long");
		used += len;
		controllers++;
	}
	if (ferror(cgroups))
		tst_brk(TBROK | TERRNO, "Failed to read /proc/cgroups");
	SAFE_FCLOSE(cgroups);
	cgroups = NULL;

	if (controllers <= 8)
		tst_brk(TCONF, "requires more than 8 enabled controllers, got %d", controllers);

	tst_res(TINFO, "Mounting controllers: %s", mount_opts);

	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (cgroups)
		SAFE_FCLOSE(cgroups);
	if (subdir_created)
		SAFE_RMDIR(SUBDIR);
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
		{"linux-git", "cfebe563bd0a3ff97e1bc167123120d59c7a84db"},
		{}
	},
};
