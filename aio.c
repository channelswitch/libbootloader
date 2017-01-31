#define _GNU_SOURCE
#include "aio.h"
#include "smount.h"
#include <libaio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <stdint.h>
#include <errno.h>

/* In order to get some interesting numbers for the progress bars, we divide
 * the reading into parts of this size. */
#define SIZE (2 << 16)

struct aio {
	int file_fd;

	io_context_t ctx;
	struct iocb command;

	int ev_fd;

	/* The buffer pointed to by @data is always the size of the file. */
	unsigned char *data;
	size_t sz, bytes_read;
};

static int resume_read(struct aio *a);
static int aio_newfree(struct aio *a, struct aio **out, const char *file, size_t *sz_out)
{
	off_t file_end;
	unsigned i;
	if(a) goto freeing;

	a = malloc(sizeof *a);
	if(!a) goto err0;

	a->file_fd = open(file, O_RDONLY | O_CLOEXEC);
	if(a->file_fd < 0) goto err1;

	a->ctx = NULL;
	if(io_setup(1, &a->ctx) < 0) goto err2;

	a->ev_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if(a->ev_fd < 0) goto err3;

	file_end = lseek(a->file_fd, 0, SEEK_END);
	if(file_end < 0 || file_end > SIZE_MAX) goto err4;
	a->sz = file_end;
	if(lseek(a->file_fd, 0, SEEK_SET) < 0) goto err4;

	a->data = malloc(a->sz);
	if(!a->data) goto err4;

	a->bytes_read = 0;
	if(resume_read(a) < 0) goto err5;

	*out = a;
	if(sz_out) *sz_out = a->sz;
	return 0;

	freeing: if(a->bytes_read < a->sz) {
		struct io_event ev;
		io_cancel(a->ctx, &a->command, &ev);
	}
	err5: free(a->data);
	err4: close(a->ev_fd);
	err3: io_destroy(a->ctx);
	err2: close(a->file_fd);
	err1: free(a);
	err0: return -1;
}

int aio_begin_read(struct aio **out, const char *filename, size_t *sz_out)
{
	return aio_newfree(NULL, out, filename, sz_out);
}

void aio_free(struct aio *a)
{
	aio_newfree(a, NULL, NULL, NULL);
}

int aio_get_fd(struct aio *a)
{
	return a->ev_fd;
}

/* Prepare and send a read command */
static int resume_read(struct aio *a)
{
	struct iocb *commands;
	size_t bytes_to_read;

	bytes_to_read = a->sz - a->bytes_read;
	if(bytes_to_read > SIZE) bytes_to_read = SIZE;

	io_prep_pread(&a->command, a->file_fd, &a->data[a->bytes_read], bytes_to_read, a->bytes_read);
	io_set_eventfd(&a->command, a->ev_fd);

	commands = &a->command;
	if(io_submit(a->ctx, 1, &commands) < 0) return -1;
	return 0;
}

int aio_process(struct aio *a, size_t *processed_out, size_t *total_out)
{
	uint64_t evs;
	if(read(a->ev_fd, &evs, sizeof evs) < 0) {
		if(errno == EAGAIN) evs = 0;
		else return -1;
	}

	while(evs) {
		struct io_event ev;
		if(io_getevents(a->ctx, 1, 1, &ev, NULL) < 0) return -1;

		a->bytes_read += ev.obj->u.c.nbytes;

		--evs;
	}

	if(total_out) *total_out = a->sz;
	if(processed_out) *processed_out = a->bytes_read;

	if(a->bytes_read < a->sz) if(resume_read(a) < 0) return -1;

	return 0;
}

unsigned char *aio_get_file_data(struct aio *a, size_t *sz_out)
{
	if(sz_out) *sz_out = a->sz;
	return a->data;
}

unsigned aio_progress_div(size_t a, size_t b, unsigned c)
{
	unsigned progress;
	if(c < b) progress = a / (b / c);
	else progress = c * a / b;

	if(a == b) progress = c;
	else if(progress == c) progress = c - 1;

	return progress;
}
