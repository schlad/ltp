// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 */

#include <errno.h>
#include <sys/socket.h>

#include <linux/rds.h>

#include "tst_test.h"

static void setup(void)
{
	int fd;

	fd = socket(AF_RDS, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		if (errno == EAFNOSUPPORT || errno == ESOCKTNOSUPPORT ||
		    errno == EPROTONOSUPPORT || errno == ENOPROTOOPT)
			tst_brk(TCONF | TERRNO, "RDS is not available");

		tst_brk(TBROK | TERRNO, "socket(AF_RDS) failed");
	}

	SAFE_CLOSE(fd);
}

static void run(void)
{
	tst_res(TPASS, "RDS is available");
}

static struct tst_test test = {
	.setup = setup,
	.test_all = run,
};
