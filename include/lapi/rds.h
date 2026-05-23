// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 SUSE LLC Sebastian Chlad <sebastian.chlad@suse.com>
 */

#ifndef LAPI_RDS_H__
#define LAPI_RDS_H__

#include <linux/rds.h>

/* Fallback for older userspace headers (e.g. openSUSE Leap 42.2). */
#ifndef RDS_CMSG_ZCOPY_COOKIE
# define RDS_CMSG_ZCOPY_COOKIE	12
#endif

#endif /* LAPI_RDS_H__ */
