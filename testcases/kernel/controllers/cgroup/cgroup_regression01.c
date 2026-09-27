// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * The test is based on old cgroup regression shell tests introduced by
 * Li Zefan <lizf@cn.fujitsu.com> in 2009.
 *
 * A cgroup (v1) named hierarchy is mounted and its tasks file is read while
 * a fork flood runs in the background. The original bug caused a kernel
 * crash on the first read of the tasks file under concurrent fork pressure.
 *
 * The bug is ancient, but the scenario is still worth covering.
 *
 * Root is required to mount and unmount the cgroup v1 hierarchy.
 *
 * Kernel: 2.6.24, 2.6.25-rcX
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"
#include "tst_atomic.h"

#define MNTPOINT	"cgroup"
#define TASKS_FILE	MNTPOINT "/tasks"
#define FORK_BATCH	200

static int mounted;
static int fd = -1;
static pid_t flood_pid = -1;
static tst_atomic_t *fork_errno;

static void fork_processes(void)
{
	int i, woken = 0;
	pid_t pid;

	for (;;) {
		for (i = 0; i < FORK_BATCH; i++) {
			/* Report errors in the parent so its mount cleanup runs. */
			pid = fork();
			if (pid < 0) {
				tst_atomic_store(errno, fork_errno);
				if (!woken)
					TST_CHECKPOINT_WAKE(0);
				exit(0);
			}
			if (!pid)
				_exit(0);
		}

		if (!woken) {
			TST_CHECKPOINT_WAKE(0);
			woken = 1;
		}

		while (wait(NULL) > 0)
			;
	}
}

static void stop_flood(void)
{
	SAFE_KILL(flood_pid, SIGKILL);
	SAFE_WAITPID(flood_pid, NULL, 0);
	flood_pid = -1;
}

static void run(void)
{
	int err;
	char buf[4096];

	tst_atomic_store(0, fork_errno);
	flood_pid = SAFE_FORK();
	if (!flood_pid)
		fork_processes();

	TST_CHECKPOINT_WAIT(0);

	fd = SAFE_OPEN(TASKS_FILE, O_RDONLY);
	while (SAFE_READ(0, fd, buf, sizeof(buf)) > 0)
		;
	SAFE_CLOSE(fd);

	stop_flood();

	err = tst_atomic_load(fork_errno);
	if (err) {
		errno = err;
		tst_brk(TBROK | TERRNO, "fork() failed in flood worker");
	}

	tst_res(TPASS, "no kernel bug was found");
}

static void setup(void)
{
	fork_errno = SAFE_MMAP(NULL, sizeof(*fork_errno), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	SAFE_MKDIR(MNTPOINT, 0755);

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression01")) {
		if (errno == ENODEV || errno == ENOENT)
			tst_brk(TCONF | TERRNO, "cgroup v1 named hierarchy not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;
}

static void cleanup(void)
{
	if (flood_pid > 0)
		stop_flood();

	if (fd != -1)
		SAFE_CLOSE(fd);

	if (mounted)
		tst_umount(MNTPOINT);

	if (fork_errno)
		SAFE_MUNMAP(fork_errno, sizeof(*fork_errno));
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.forks_child = 1,
	.needs_checkpoints = 1,
	.timeout = 30,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "0e04388f0189fa1f6812a8e1cb6172136eada87e"},
		{}
	},
};
