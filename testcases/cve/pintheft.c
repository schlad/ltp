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
	int val;

	fd = socket(AF_RDS, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		if (errno == EAFNOSUPPORT || errno == ESOCKTNOSUPPORT ||
		    errno == EPROTONOSUPPORT || errno == ENOPROTOOPT)
			tst_brk(TCONF | TERRNO, "RDS is not available");

		tst_brk(TBROK | TERRNO, "socket(AF_RDS) failed");
	}

	val = RDS_TRANS_TCP;
	if (setsockopt(fd, SOL_RDS, SO_RDS_TRANSPORT, &val, sizeof(val))) {
		if (errno == ENOPROTOOPT || errno == EINVAL)
			tst_brk(TCONF | TERRNO, "RDS TCP transport is not available");

		tst_brk(TBROK | TERRNO, "setsockopt(SO_RDS_TRANSPORT) failed");
	}

	SAFE_CLOSE(fd);
}

static void run(void)
{
	tst_res(TPASS, "RDS TCP transport is available");
}

static struct tst_test test = {
	.setup = setup,
	.test_all = run,
};
