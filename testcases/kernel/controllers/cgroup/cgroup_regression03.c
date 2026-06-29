// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Regression test for NULL cgrp->dentry access when reading the scheduler
 * debug file while concurrently creating and removing cpu cgroups.
 *
 * The scheduler debug file prints the cgroup path of every task group, so
 * the cgroups have to be created in a hierarchy with the cpu controller.
 * The file is /proc/sched_debug or, since v5.13, <debugfs>/sched/debug.
 *
 * Root is required to create cgroups and to read the scheduler debug file.
 *
 * Kernel: 2.6.26-2.6.28
 *
 * References:
 *
 * - http://lkml.org/lkml/2008/10/30/44
 * - http://lkml.org/lkml/2008/12/12/107
 * - http://lkml.org/lkml/2008/12/16/481
 */

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "tst_test.h"
#include "tst_atomic.h"

#define CHILD_CGROUP "cgroup_regression03"

static const char *sched_debug;
static int sched_fd = -1;
static int cg_fd = -1;
static pid_t worker_pid = -1;
static tst_atomic_t *stop_worker;

/*
 * /proc/sched_debug was moved to debugfs in v5.13 by commit d27e9ae2f244
 * ("sched: Move /proc/sched_debug to debugfs").
 */
static const char *sched_debug_path(void)
{
	static const char *const paths[] = {
		"/proc/sched_debug",
		"/sys/kernel/debug/sched/debug",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		if (!access(paths[i], R_OK))
			return paths[i];
	}

	return NULL;
}

static void mkdir_rmdir_loop(void)
{
	while (!tst_atomic_load(stop_worker) && tst_remaining_runtime()) {
		TEST(mkdirat(cg_fd, CHILD_CGROUP, 0755));
		if (!TST_RET)
			TEST(unlinkat(cg_fd, CHILD_CGROUP, AT_REMOVEDIR));
		if (TST_RET) {
			tst_res(TINFO | TTERRNO, "cgroup create/remove failed");
			tst_atomic_store(1, stop_worker);
			exit(1);
		}
	}

	exit(0);
}

static void stop_and_reap_worker(void)
{
	if (worker_pid == -1)
		return;

	/* Let the worker remove its current cgroup before it exits. */
	tst_atomic_store(1, stop_worker);
	tst_reap_children();
	worker_pid = -1;
}

static void run(void)
{
	char buf[4096];

	tst_atomic_store(0, stop_worker);
	worker_pid = SAFE_FORK();
	if (!worker_pid)
		mkdir_rmdir_loop();

	while (!tst_atomic_load(stop_worker) && tst_remaining_runtime()) {
		sched_fd = SAFE_OPEN(sched_debug, O_RDONLY);
		while (SAFE_READ(0, sched_fd, buf, sizeof(buf)) > 0)
			;
		SAFE_CLOSE(sched_fd);
	}

	stop_and_reap_worker();

	tst_res(TPASS, "no kernel bug was found");
}

static void setup(void)
{
	sched_debug = sched_debug_path();
	if (!sched_debug)
		tst_brk(TCONF, "scheduler debug file not available");

	tst_res(TINFO, "Using %s", sched_debug);

	stop_worker = SAFE_MMAP(NULL, sizeof(*stop_worker), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (!TST_CG_VER_IS_V1(tst_cg, "cpu"))
		SAFE_CG_PRINT(tst_cg, "cgroup.subtree_control", "+cpu");
	cg_fd = tst_cg_group_dir_fd(tst_cg, "cpu");
}

static void cleanup(void)
{
	stop_and_reap_worker();

	/* The worker may have failed after creating the directory. */
	if (cg_fd != -1 && unlinkat(cg_fd, CHILD_CGROUP, AT_REMOVEDIR) &&
	    errno != ENOENT)
		tst_res(TWARN | TERRNO, "unlinkat(%s) failed", CHILD_CGROUP);

	if (sched_fd != -1)
		SAFE_CLOSE(sched_fd);

	if (stop_worker)
		SAFE_MUNMAP(stop_worker, sizeof(*stop_worker));
}

static struct tst_test test = {
	.needs_root = 1,
	.forks_child = 1,
	.runtime = 30,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.needs_cgroup_ctrls = (const char *const []){ "cpu", NULL },
	.tags = (const struct tst_tag[]) {
		{"linux-git", "a47295e6bc42ad35f9c15ac66f598aa24debd4e2"},
		{}
	},
};
