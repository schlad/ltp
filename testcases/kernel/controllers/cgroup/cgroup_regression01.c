// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * The test is based on old cgroup regression shell tests introduced by
 * Li Zefan <lizf@cn.fujitsu.com> in 2009.
 *
 * A cgroup (v1) named hierarchy is mounted while a fork flood runs in the
 * background. Two operations are exercised under concurrent fork pressure:
 *
 * - Reading the tasks file. The original bug caused a kernel crash on the
 *   first read of the tasks file under concurrent fork pressure.
 *   Kernel: 2.6.24, 2.6.25-rcX
 *   Fixed by: commit 0e04388f0189fa1f6812a8e1cb6172136eada87e
 *
 *   References:
 *   - http://lkml.org/lkml/2007/10/17/224
 *   - http://lkml.org/lkml/2008/3/5/332
 *   - http://lkml.org/lkml/2008/4/16/493
 *
 * - Writing to the tasks file to assign the current process to the cgroup,
 *   exercising the task assignment path (``cgroup_attach_task()``) under the
 *   same pressure.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tst_test.h"

#define MNTPOINT	"cgroup"
#define TASKS_FILE	MNTPOINT "/tasks"

static int mounted;
static pid_t flood_pid = -1;

static void fork_processes(void)
{
	int i;
	pid_t pid;
	int woke = 0;

	while (1) {
		for (i = 0; i < 200; i++) {
			pid = fork();
			if (pid == 0)
				exit(0);
			else if (pid == -1)
				continue;
			if (!woke) {
				TST_CHECKPOINT_WAKE(0);
				woke = 1;
			}
		}

		for (i = 0; i < 200; i++)
			if (wait(NULL) < 0)
				break;
	}
}

static void run(void)
{
	int fd;
	char buf[4096];
	char pid_str[32];
	ssize_t n;
	int len;

	flood_pid = SAFE_FORK();
	if (flood_pid == 0) {
		fork_processes();
		exit(0);
	}

	TST_CHECKPOINT_WAIT(0);

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression01")) {
		if (errno == ENODEV || errno == ENOENT)
			tst_brk(TCONF, "cgroup named hierarchy not available");
		tst_brk(TBROK | TERRNO, "Failed to mount cgroup filesystem");
	}
	mounted = 1;

	fd = SAFE_OPEN(TASKS_FILE, O_RDONLY);

	while ((n = read(fd, buf, sizeof(buf))) > 0)
		;
	if (n < 0)
		tst_res(TFAIL | TERRNO, "read from %s failed", TASKS_FILE);
	else
		tst_res(TPASS, "tasks file read under fork pressure: no kernel bug");
	SAFE_CLOSE(fd);

	fd = SAFE_OPEN(TASKS_FILE, O_WRONLY);

	len = snprintf(pid_str, sizeof(pid_str), "%d", getpid());
	TST_EXP_VAL(write(fd, pid_str, len), (ssize_t)len,
		"write PID to %s under fork pressure", TASKS_FILE);

	SAFE_CLOSE(fd);

	if (tst_umount(MNTPOINT))
		tst_brk(TBROK | TERRNO, "umount failed");
	mounted = 0;

	kill(flood_pid, SIGTERM);
	SAFE_WAITPID(flood_pid, NULL, 0);
	flood_pid = -1;
}

static void setup(void)
{
	SAFE_MKDIR(MNTPOINT, 0755);
}

static void cleanup(void)
{
	if (flood_pid > 0) {
		kill(flood_pid, SIGTERM);
		SAFE_WAITPID(flood_pid, NULL, 0);
	}

	if (mounted)
		tst_umount(MNTPOINT);
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.needs_checkpoints = 1,
	.forks_child = 1,
	.timeout = 30,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.tags = (const struct tst_tag[]) {
		{"linux-git", "0e04388f0189"},
		{}
	},
};
