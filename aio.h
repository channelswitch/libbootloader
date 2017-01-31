#include <stddef.h>
struct aio;
int aio_begin_read(struct aio **out, const char *filename, size_t *sz_out);
void aio_free(struct aio *a);

int aio_get_fd(struct aio *a);
int aio_process(struct aio *a, size_t *processed_out, size_t *total_out);

unsigned char *aio_get_file_data(struct aio *a, size_t *sz_out);

/* Returns (a / b) * c, guaranteed only to be equal to c if a == b. */
unsigned aio_progress_div(size_t a, size_t b, unsigned c);
