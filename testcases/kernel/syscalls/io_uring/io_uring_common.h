// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 IBM
 * Author: Sachin Sant <sachinp@linux.ibm.com>
 *
 * Copyright (C) 2026 Sebastian Chlad <sebastian.chlad@suse.com>
 *
 * Common definitions and helper functions for io_uring tests
 */

#ifndef IO_URING_COMMON_H
#define IO_URING_COMMON_H

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "config.h"
#include "tst_test.h"
#include "lapi/io_uring.h"

/* Common structures for io_uring ring management */
struct io_sq_ring {
	unsigned int *head;
	unsigned int *tail;
	unsigned int *ring_mask;
	unsigned int *ring_entries;
	unsigned int *flags;
	unsigned int *array;
};

struct io_cq_ring {
	unsigned int *head;
	unsigned int *tail;
	unsigned int *ring_mask;
	unsigned int *ring_entries;
	struct io_uring_cqe *cqes;
};

struct io_uring_submit {
	int ring_fd;
	struct io_sq_ring sq_ring;
	struct io_uring_sqe *sqes;
	struct io_cq_ring cq_ring;
	void *sq_ptr;
	size_t sq_ptr_size;
	void *cq_ptr;
	size_t cq_ptr_size;
	unsigned int sq_pending;
};

/*
 * Setup io_uring instance with specified queue depth and optional flags
 * Returns 0 on success, -1 on failure
 */
static inline int io_uring_setup_queue(struct io_uring_submit *s,
				       unsigned int queue_depth,
				       unsigned int flags)
{
	struct io_sq_ring *sring = &s->sq_ring;
	struct io_cq_ring *cring = &s->cq_ring;
	struct io_uring_params p;

	memset(&p, 0, sizeof(p));
	p.flags = flags;
	s->ring_fd = io_uring_setup(queue_depth, &p);
	if (s->ring_fd < 0) {
		tst_brk(TBROK | TERRNO, "io_uring_setup() failed");
		return -1;
	}

	s->sq_ptr_size = p.sq_off.array + p.sq_entries * sizeof(unsigned int);

	s->sq_ptr = SAFE_MMAP(0, s->sq_ptr_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_POPULATE, s->ring_fd,
			      IORING_OFF_SQ_RING);

	/* Save submission queue pointers */
	sring->head = s->sq_ptr + p.sq_off.head;
	sring->tail = s->sq_ptr + p.sq_off.tail;
	sring->ring_mask = s->sq_ptr + p.sq_off.ring_mask;
	sring->ring_entries = s->sq_ptr + p.sq_off.ring_entries;
	sring->flags = s->sq_ptr + p.sq_off.flags;
	sring->array = s->sq_ptr + p.sq_off.array;

	s->sqes = SAFE_MMAP(0, p.sq_entries * sizeof(struct io_uring_sqe),
			    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			    s->ring_fd, IORING_OFF_SQES);

	s->cq_ptr_size = p.cq_off.cqes +
			 p.cq_entries * sizeof(struct io_uring_cqe);

	s->cq_ptr = SAFE_MMAP(0, s->cq_ptr_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_POPULATE, s->ring_fd,
			      IORING_OFF_CQ_RING);

	/* Save completion queue pointers */
	cring->head = s->cq_ptr + p.cq_off.head;
	cring->tail = s->cq_ptr + p.cq_off.tail;
	cring->ring_mask = s->cq_ptr + p.cq_off.ring_mask;
	cring->ring_entries = s->cq_ptr + p.cq_off.ring_entries;
	cring->cqes = s->cq_ptr + p.cq_off.cqes;

	return 0;
}

/*
 * Cleanup io_uring instance and unmap all memory regions
 */
static inline void io_uring_cleanup_queue(struct io_uring_submit *s,
					  unsigned int queue_depth)
{
	if (s->sqes)
		SAFE_MUNMAP(s->sqes, queue_depth * sizeof(struct io_uring_sqe));
	if (s->cq_ptr)
		SAFE_MUNMAP(s->cq_ptr, s->cq_ptr_size);
	if (s->sq_ptr)
		SAFE_MUNMAP(s->sq_ptr, s->sq_ptr_size);
	if (s->ring_fd > 0)
		SAFE_CLOSE(s->ring_fd);
}

/*
 * Get the next available SQE slot from the submission ring.
 * The SQE is zeroed and tracked as pending until io_uring_submit() is called.
 */
static inline struct io_uring_sqe *io_uring_get_sqe(struct io_uring_submit *s)
{
	struct io_sq_ring *sring = &s->sq_ring;
	unsigned int index = (*sring->tail + s->sq_pending) & *sring->ring_mask;
	struct io_uring_sqe *sqe = &s->sqes[index];

	memset(sqe, 0, sizeof(*sqe));
	sring->array[index] = index;
	s->sq_pending++;

	return sqe;
}

/*
 * Generic SQE fill for read/write family operations.
 * Does not touch user_data - caller sets it via io_uring_sqe_set_data64().
 */
static inline void io_uring_prep_rw(struct io_uring_sqe *sqe, int opcode,
				    int fd, const void *addr, unsigned int len,
				    off_t offset)
{
	sqe->opcode = opcode;
	sqe->fd = fd;
	sqe->addr = (unsigned long)addr;
	sqe->len = len;
	sqe->off = offset;
}

static inline void io_uring_prep_read(struct io_uring_sqe *sqe, int fd,
				      void *buf, size_t len, off_t offset)
{
	io_uring_prep_rw(sqe, IORING_OP_READ, fd, buf, len, offset);
}

static inline void io_uring_prep_write(struct io_uring_sqe *sqe, int fd,
				       const void *buf, size_t len, off_t offset)
{
	io_uring_prep_rw(sqe, IORING_OP_WRITE, fd, buf, len, offset);
}

static inline void io_uring_prep_readv(struct io_uring_sqe *sqe, int fd,
				       struct iovec *iovs, int nr_vecs,
				       off_t offset)
{
	io_uring_prep_rw(sqe, IORING_OP_READV, fd, iovs, nr_vecs, offset);
}

static inline void io_uring_prep_writev(struct io_uring_sqe *sqe, int fd,
					struct iovec *iovs, int nr_vecs,
					off_t offset)
{
	io_uring_prep_rw(sqe, IORING_OP_WRITEV, fd, iovs, nr_vecs, offset);
}

/*
 * Set the user_data field of an SQE. user_data is returned verbatim in the
 * corresponding CQE and must be unique per in-flight request to allow correct
 * correlation of completions.
 */
static inline void io_uring_sqe_set_data64(struct io_uring_sqe *sqe,
					   uint64_t data)
{
	sqe->user_data = data;
}

/*
 * Submit all pending SQEs to the kernel.
 */
static inline void io_uring_submit(struct io_uring_submit *s)
{
	unsigned int pending = s->sq_pending;
	unsigned int tail;

	if (!pending)
		return;

	tail = *s->sq_ring.tail + pending;
	__atomic_store(s->sq_ring.tail, &tail, __ATOMIC_RELEASE);
	s->sq_pending = 0;

	if (io_uring_enter(s->ring_fd, pending, 0, 0, NULL) < 0)
		tst_brk(TBROK | TERRNO, "io_uring_enter() failed");
}

/*
 * Wait for the next CQE and return a pointer to it.
 * Does not advance the CQ head - call io_uring_cqe_seen() when done.
 */
static inline struct io_uring_cqe *io_uring_cqe_wait(struct io_uring_submit *s,
						      sigset_t *sig)
{
	struct io_cq_ring *cring = &s->cq_ring;
	unsigned int cq_tail;
	int ret;

	ret = io_uring_enter(s->ring_fd, 0, 1, IORING_ENTER_GETEVENTS, sig);
	if (ret < 0)
		tst_brk(TBROK | TERRNO, "io_uring_enter() failed");

	__atomic_load(cring->tail, &cq_tail, __ATOMIC_ACQUIRE);
	if (*cring->head == cq_tail)
		tst_brk(TBROK, "No completion event received");

	return &cring->cqes[*cring->head & *cring->ring_mask];
}

/*
 * Mark the current CQE as consumed, advancing the CQ head.
 */
static inline void io_uring_cqe_seen(struct io_uring_submit *s)
{
	unsigned int head = *s->cq_ring.head + 1;

	__atomic_store(s->cq_ring.head, &head, __ATOMIC_RELEASE);
}

#endif /* IO_URING_COMMON_H */
