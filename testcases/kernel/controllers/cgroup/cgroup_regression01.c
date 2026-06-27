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
 * While the bug is ancient the scenario makes still some sense.
 *
 * Kernel: 2.6.24, 2.6.25-rcX
 * Fixed by: commit 0e04388f0189fa1f6812a8e1cb6172136eada87e
 *
 * References:
 * - http://lkml.org/lkml/2007/10/17/224
 * - http://lkml.org/lkml/2008/3/5/332
 * - http://lkml.org/lkml/2008/4/16/493
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

	while (1) {
		for (i = 0; i < 200; i++) {
			pid = fork();
			if (pid == 0) {
				exit(0);
			} else if (pid == -1) {
				continue;
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

	flood_pid = fork();
	if (flood_pid < 0)
		tst_brk(TBROK | TERRNO, "fork failed");
	if (flood_pid == 0)
		fork_processes();

	sleep(1);

	if (mount("cgroup", MNTPOINT, "cgroup", 0, "none,name=cgroup_regression01")) {
		if (errno == ENODEV)
			tst_brk(TCONF, "cgroup filesystem not available");
		tst_res(TFAIL | TERRNO, "Failed to mount cgroup filesystem");
		goto cleanup_forked_processes;
	}
	mounted = 1;

	tst_res(TINFO, "mounted %s, contents:", MNTPOINT);
	DIR *dir = opendir(MNTPOINT);
	if (dir) {
		struct dirent *entry;

		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_name[0] != '.')
				tst_res(TINFO, "  %s", entry->d_name);
		}
		closedir(dir);
	}

	fd = open(TASKS_FILE, O_RDONLY);
	if (fd < 0) {
		tst_res(TFAIL | TERRNO, "Failed to open %s", TASKS_FILE);
		goto cleanup_mount;
	}

	while (read(fd, buf, sizeof(buf)) > 0)
		;
	SAFE_CLOSE(fd);

	tst_res(TPASS, "no kernel bug was found");

cleanup_mount:
	tst_umount(MNTPOINT);
	mounted = 0;
cleanup_forked_processes:
	kill(flood_pid, SIGTERM);
	waitpid(flood_pid, NULL, 0);
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
		waitpid(flood_pid, NULL, 0);
	}

	if (mounted)
		tst_umount(MNTPOINT);
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
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
