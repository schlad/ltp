// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 IBM
 * Author: Sachin Sant <sachinp@linux.ibm.com>
 *
 * Copyright (C) 2026 Sebastian Chlad <sebastian.chlad@suse.com>
 *
 */
/*
 * Test IORING_OP_READ and IORING_OP_WRITE operations.
 *
 * This test validates basic read and write operations using io_uring.
 * It tests:
 * 1. IORING_OP_WRITE - Writing data to a file
 * 2. IORING_OP_READ - Reading data from a file
 * 3. Data integrity verification
 */

#include "io_uring_common.h"

#define TEST_FILE "io_uring_test_file"
#define QUEUE_DEPTH 2
#define BLOCK_SZ 4096

static char *write_buf;
static char *read_buf;
static struct io_uring_submit s;
static sigset_t sig;

static void init_buffer(char start_char)
{
	size_t i;

	for (i = 0; i < BLOCK_SZ; i++)
		write_buf[i] = start_char + (i % 26);
}

static void verify_data_integrity(const char *test_name)
{
	size_t i;

	if (memcmp(write_buf, read_buf, BLOCK_SZ) == 0) {
		tst_res(TPASS, "%s data integrity verified", test_name);
	} else {
		tst_res(TFAIL, "%s data mismatch", test_name);
		for (i = 0; i < BLOCK_SZ && i < 64; i++) {
			if (write_buf[i] != read_buf[i]) {
				tst_res(TINFO, "First mismatch at offset %zu: "
					"wrote 0x%02x, read 0x%02x",
					i, write_buf[i], read_buf[i]);
				break;
			}
		}
	}
}

static void test_write_read(void)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int fd;

	init_buffer('A');
	fd = SAFE_OPEN(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);

	tst_res(TINFO, "Testing IORING_OP_WRITE");
	sqe = io_uring_get_sqe(&s);
	io_uring_prep_write(sqe, fd, write_buf, BLOCK_SZ, 0);
	io_uring_sqe_set_data64(sqe, 1);
	io_uring_submit(&s);

	cqe = io_uring_cqe_wait(&s, &sig);
	if (cqe->res != BLOCK_SZ)
		tst_brk(TBROK, "IORING_OP_WRITE failed: res=%d", cqe->res);
	tst_res(TPASS, "IORING_OP_WRITE: %d bytes written", cqe->res);
	io_uring_cqe_seen(&s);

	SAFE_FSYNC(fd);

	tst_res(TINFO, "Testing IORING_OP_READ");
	memset(read_buf, 0, BLOCK_SZ);
	sqe = io_uring_get_sqe(&s);
	io_uring_prep_read(sqe, fd, read_buf, BLOCK_SZ, 0);
	io_uring_sqe_set_data64(sqe, 2);
	io_uring_submit(&s);

	cqe = io_uring_cqe_wait(&s, &sig);
	if (cqe->res != BLOCK_SZ)
		tst_brk(TBROK, "IORING_OP_READ failed: res=%d", cqe->res);
	tst_res(TPASS, "IORING_OP_READ: %d bytes read", cqe->res);
	io_uring_cqe_seen(&s);

	verify_data_integrity("Basic I/O");
	SAFE_CLOSE(fd);
}

static void test_partial_io(void)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	size_t half = BLOCK_SZ / 2;
	int fd;

	tst_res(TINFO, "Testing partial I/O operations");
	init_buffer('a');
	fd = SAFE_OPEN(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);

	sqe = io_uring_get_sqe(&s);
	io_uring_prep_write(sqe, fd, write_buf, half, 0);
	io_uring_sqe_set_data64(sqe, 1);
	io_uring_submit(&s);

	cqe = io_uring_cqe_wait(&s, &sig);
	if (cqe->res != (int)half)
		tst_brk(TBROK, "IORING_OP_WRITE failed: res=%d", cqe->res);
	tst_res(TPASS, "IORING_OP_WRITE: %d bytes written at offset 0", cqe->res);
	io_uring_cqe_seen(&s);

	sqe = io_uring_get_sqe(&s);
	io_uring_prep_write(sqe, fd, write_buf + half, half, half);
	io_uring_sqe_set_data64(sqe, 2);
	io_uring_submit(&s);

	cqe = io_uring_cqe_wait(&s, &sig);
	if (cqe->res != (int)half)
		tst_brk(TBROK, "IORING_OP_WRITE failed: res=%d", cqe->res);
	tst_res(TPASS, "IORING_OP_WRITE: %d bytes written at offset %zu",
		cqe->res, half);
	io_uring_cqe_seen(&s);

	SAFE_FSYNC(fd);

	memset(read_buf, 0, BLOCK_SZ);
	sqe = io_uring_get_sqe(&s);
	io_uring_prep_read(sqe, fd, read_buf, BLOCK_SZ, 0);
	io_uring_sqe_set_data64(sqe, 3);
	io_uring_submit(&s);

	cqe = io_uring_cqe_wait(&s, &sig);
	if (cqe->res != BLOCK_SZ)
		tst_brk(TBROK, "IORING_OP_READ failed: res=%d", cqe->res);
	tst_res(TPASS, "IORING_OP_READ: %d bytes read", cqe->res);
	io_uring_cqe_seen(&s);

	verify_data_integrity("Partial I/O");
	SAFE_CLOSE(fd);
}

static void run(void)
{
	test_write_read();
	test_partial_io();
}

static void setup(void)
{
	io_uring_setup_supported_by_kernel();
	sigemptyset(&sig);
	memset(&s, 0, sizeof(s));
	io_uring_setup_queue(&s, QUEUE_DEPTH, 0);
}

static void cleanup(void)
{
	io_uring_cleanup_queue(&s, QUEUE_DEPTH);
}

static struct tst_test test = {
	.test_all = run,
	.setup = setup,
	.cleanup = cleanup,
	.needs_tmpdir = 1,
	.bufs = (struct tst_buffers []) {
		{&write_buf, .size = BLOCK_SZ},
		{&read_buf, .size = BLOCK_SZ},
		{}
	},
	.save_restore = (const struct tst_path_val[]) {
		{PATH_KERN_IO_URING_DISABLED, "0",
			TST_SR_SKIP_MISSING | TST_SR_TCONF_RO},
		{}
	}
};
