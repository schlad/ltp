// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 SUSE LLC
 */

#include "tst_test.h"

static void run(void)
{
	tst_res(TPASS, "hello world");
}

static struct tst_test test = {
	.test_all = run,
};
