// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 * Author: Sebastian Chlad <sebastian.chlad@suse.com>
 */

/*\
 * Race cgroup mount/unmount against directory creation/removal and against
 * reading release_agent. These exercise the VFS __mntput locking regression
 * and the false positive lockdep warning caused by missing s_umount
 * annotations, respectively.
 *
 * Both cases check kernel taint. The release_agent case additionally requires
 * active lockdep and checks debug_locks after the race, since lockdep reports
 * do not taint the kernel.
 *
 * Root is required to mount and unmount the named cgroup v1 hierarchy.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <unistd.h>
#include "tst_test.h"
#include "tst_atomic.h"

#define MNTPOINT "cgroup"
#define SUBDIR MNTPOINT "/0"
#define RELEASE_AGENT MNTPOINT "/release_agent"
#define LOCKDEP_STATS "/proc/lockdep_stats"

static struct tcase {
	const char *desc;
	int read_release_agent;
} tcases[] = {
	{"mkdir/rmdir", 0},
	{"read release_agent", 1},
};

static struct worker_state {
	tst_atomic_t stop;
	unsigned long mounts;
	int mount_error;
	int umount_error;
} *worker;
static pid_t worker_pid = -1;
static int agent_fd = -1;
static char mount_opts[128];

static int lockdep_debug_locks(void)
{
	int val;

	if (access(LOCKDEP_STATS, F_OK))
		return -1;

	SAFE_FILE_LINES_SCANF(LOCKDEP_STATS, " debug_locks: %d", &val);
	return val;
}

static void mount_worker(void)
{
	while (!tst_atomic_load(&worker->stop) && tst_remaining_runtime()) {
		if (!mount("none", MNTPOINT, "cgroup", 0, mount_opts)) {
			worker->mounts++;
		} else if (errno != EBUSY) {
			worker->mount_error = errno;
			break;
		}

		if (umount(MNTPOINT) && errno != EINVAL && errno != EBUSY) {
			worker->umount_error = errno;
			break;
		}
	}

	exit(0);
}

static void stop_worker(void)
{
	int status;

	if (worker_pid == -1)
		return;

	tst_atomic_store(1, &worker->stop);
	SAFE_WAITPID(worker_pid, &status, 0);
	worker_pid = -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		tst_brk(TBROK, "Mount worker exited abnormally: %#x", status);
}

static void drain_mounts(void)
{
	if (rmdir(SUBDIR) && errno != ENOENT)
		tst_res(TWARN | TERRNO, "Failed to remove %s", SUBDIR);

	while (!umount(MNTPOINT))
		;
	if (errno != EINVAL && errno != ENOENT)
		tst_brk(TBROK | TERRNO, "Failed to unmount %s", MNTPOINT);
}

static void run(unsigned int n)
{
	struct tcase *tc = &tcases[n];
	unsigned long mounts = 0, operations = 0;
	char buf[4096];

	if (tc->read_release_agent && lockdep_debug_locks() != 1) {
		tst_res(TCONF, "release_agent case requires active lockdep");
		return;
	}

	memset(worker, 0, sizeof(*worker));
	tst_res(TINFO, "Racing mount/unmount with %s", tc->desc);
	worker_pid = SAFE_FORK();
	if (!worker_pid)
		mount_worker();

	while (tst_remaining_runtime()) {
		TEST(mount("none", MNTPOINT, "cgroup", 0, mount_opts));
		if (TST_RET) {
			if (TST_ERR == EBUSY)
				continue;
			tst_brk(TBROK | TTERRNO, "mount failed");
		}
		mounts++;

		if (tc->read_release_agent) {
			agent_fd = open(RELEASE_AGENT, O_RDONLY);
			if (agent_fd != -1) {
				while (SAFE_READ(0, agent_fd, buf, sizeof(buf)) > 0)
					operations++;
				SAFE_CLOSE(agent_fd);
			} else if (errno != ENOENT) {
				tst_brk(TBROK | TERRNO, "open release_agent failed");
			}
		} else {
			if (!mkdir(SUBDIR, 0755))
				operations++;
			else if (errno != EEXIST && errno != ENOENT)
				tst_brk(TBROK | TERRNO, "mkdir failed");
			if (rmdir(SUBDIR) && errno != ENOENT && errno != EBUSY)
				tst_brk(TBROK | TERRNO, "rmdir failed");
		}

		if (umount(MNTPOINT) && errno != EINVAL && errno != EBUSY)
			tst_brk(TBROK | TERRNO, "umount failed");
	}

	stop_worker();
	drain_mounts();
	if (worker->mount_error || worker->umount_error) {
		errno = worker->mount_error ?: worker->umount_error;
		tst_brk(TBROK | TERRNO, "Worker %s failed",
			worker->mount_error ? "mount" : "umount");
	}

	tst_res(TINFO, "Successful mounts: %lu + %lu; operations: %lu",
		mounts, worker->mounts, operations);
	if (!mounts || !worker->mounts || !operations)
		tst_brk(TBROK, "Race workload did not execute");

	if (tc->read_release_agent && lockdep_debug_locks() != 1)
		tst_res(TFAIL, "lockdep reported a problem, see dmesg");
	else
		tst_res(TPASS, "%s race completed without a kernel warning", tc->desc);
}

static void setup(void)
{
	snprintf(mount_opts, sizeof(mount_opts), "none,name=ltp_reg05_%ld",
		 (long)getpid());
	SAFE_MKDIR(MNTPOINT, 0755);

	TEST(mount("none", MNTPOINT, "cgroup", 0, mount_opts));
	if (TST_RET) {
		if (TST_ERR == ENODEV || TST_ERR == ENOENT)
			tst_brk(TCONF | TTERRNO, "Named cgroup v1 hierarchy unavailable");
		tst_brk(TBROK | TTERRNO, "Failed to mount cgroup filesystem");
	}
	SAFE_UMOUNT(MNTPOINT);

	worker = SAFE_MMAP(NULL, sizeof(*worker), PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

static void cleanup(void)
{
	stop_worker();
	if (agent_fd != -1)
		SAFE_CLOSE(agent_fd);
	drain_mounts();
	if (worker)
		SAFE_MUNMAP(worker, sizeof(*worker));
}

static struct tst_test test = {
	.needs_root = 1,
	.needs_tmpdir = 1,
	.forks_child = 1,
	.runtime = 30,
	.taint_check = TST_TAINT_W | TST_TAINT_D,
	.setup = setup,
	.cleanup = cleanup,
	.test = run,
	.tcnt = ARRAY_SIZE(tcases),
	.tags = (const struct tst_tag[]) {
		{"linux-git", "1a88b5364b535edaa321d70a566e358390ff0872"},
		{"linux-git", "ada723dcd681e2dffd7d73345cc8fda0eb0df9bd"},
		{}
	},
};
