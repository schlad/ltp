// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 ARM Ltd. All rights reserved.
 * Author: Vikas Kumar <vikas.kumar2@arm.com>
 *
 * Copyright (C) 2020 Cyril Hrubis <chrubis@suse.cz>
 *
 * Copyright (C) 2026 Sebastian Chlad <sebastian.chlad@suse.com>
 *
 * Tests for asynchronous I/O raw API i.e io_uring_setup(), io_uring_register()
 * and io_uring_enter(). This test validates basic API operation by creating a
 * submission queue and a completion queue using io_uring_setup(). User buffer
 * registered in the kernel for long term operation using io_uring_register().
 * This test initiates I/O operations using io_uring_submit() and
 * io_uring_cqe_wait() helpers built on top of io_uring_enter().
 */
#include "io_uring_common.h"

#define TEST_FILE "test_file"

#define QUEUE_DEPTH 1
#define BLOCK_SZ    1024

static struct tcase {
	unsigned int setup_flags;
	unsigned int register_opcode;
	unsigned int enter_flags;
} tcases[] = {
	{0, IORING_REGISTER_BUFFERS, IORING_OP_READ_FIXED},
};

static struct io_uring_submit s;
static struct iovec *iov;

static int setup_io_uring_test(struct io_uring_submit *s, struct tcase *tc)
{
	int ret;

	ret = io_uring_setup_queue(s, QUEUE_DEPTH, tc->setup_flags);
	if (ret == 0)
		tst_res(TPASS, "io_uring_setup() passed");

	return ret;
}

static void check_buffer(char *buffer, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buffer[i] != 'a') {
			tst_res(TFAIL, "Wrong data at offset %zu", i);
			break;
		}
	}

	if (i == len)
		tst_res(TPASS, "Buffer filled in correctly");
}

static void drain_uring_cq(struct io_uring_submit *s, unsigned int exp_events)
{
	unsigned int events = 0;
	struct io_uring_cqe *cqe;
	struct iovec *iovecs;

	while (events < exp_events) {
		cqe = io_uring_cqe_wait(s, NULL);
		events++;

		if (cqe->res < 0) {
			tst_res(TFAIL, "CQE result %s", tst_strerrno(-cqe->res));
		} else {
			iovecs = (void *)cqe->user_data;

			if (cqe->res == BLOCK_SZ)
				tst_res(TPASS, "CQE result %i", cqe->res);
			else
				tst_res(TFAIL, "CQE result %i expected %i",
					cqe->res, BLOCK_SZ);

			check_buffer(iovecs[0].iov_base, cqe->res);
		}

		io_uring_cqe_seen(s);
	}

	if (exp_events == events)
		tst_res(TPASS, "Got %u completion events", events);
	else
		tst_res(TFAIL, "Got %u completion events expected %u",
			events, exp_events);
}

static int submit_to_uring_sq(struct io_uring_submit *s, struct tcase *tc)
{
	struct io_uring_sqe *sqe;
	int fd;

	memset(iov->iov_base, 0, iov->iov_len);

	if (io_uring_register(s->ring_fd, tc->register_opcode,
			      iov, QUEUE_DEPTH)) {
		tst_res(TFAIL | TERRNO, "io_uring_register() failed");
		return 1;
	}
	tst_res(TPASS, "io_uring_register() passed");

	fd = SAFE_OPEN(TEST_FILE, O_RDONLY);

	sqe = io_uring_get_sqe(s);
	io_uring_prep_rw(sqe, tc->enter_flags, fd, iov->iov_base, BLOCK_SZ, 0);
	io_uring_sqe_set_data64(sqe, (uint64_t)iov);
	io_uring_submit(s);

	SAFE_CLOSE(fd);
	return 0;
}

static void cleanup_io_uring_test(void)
{
	io_uring_register(s.ring_fd, IORING_UNREGISTER_BUFFERS,
			  NULL, QUEUE_DEPTH);
	io_uring_cleanup_queue(&s, QUEUE_DEPTH);
}

static void run(unsigned int n)
{
	struct tcase *tc = &tcases[n];

	if (setup_io_uring_test(&s, tc))
		return;

	if (!submit_to_uring_sq(&s, tc))
		drain_uring_cq(&s, 1);

	cleanup_io_uring_test();
}

static void setup(void)
{
	io_uring_setup_supported_by_kernel();
	tst_fill_file(TEST_FILE, 'a', 1024, 1);
}

static struct tst_test test = {
	.setup = setup,
	.test = run,
	.needs_tmpdir = 1,
	.tcnt = ARRAY_SIZE(tcases),
	.bufs = (struct tst_buffers []) {
		{&iov, .iov_sizes = (int[]){BLOCK_SZ, -1}},
		{}
	},
	.save_restore = (const struct tst_path_val[]) {
		{PATH_KERN_IO_URING_DISABLED, "0",
			TST_SR_SKIP_MISSING | TST_SR_TCONF_RO},
		{}
	}
};
