#include "kexec.h"
#include "s.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <asm/e820.h>
#include <sys/syscall.h>
#include <fcntl.h>

#define KEXEC_SEGMENT_MAX 16

struct kexec_segment {
	void *buf;
	size_t bufsz;
	unsigned long mem;
	size_t memsz;
};

struct kexec {
	unsigned n_e820, n_segs;
	size_t page_size;
	struct kexec_e820 e820[E820MAX];
	struct kexec_segment segs[KEXEC_SEGMENT_MAX];
};

static int kexec_newfree(struct kexec **k_out, struct kexec *k)
{
	if(k) goto freeing;

	k = malloc(sizeof *k);
	if(!k) goto err0;

	k->n_e820 = 0;
	k->n_segs = 0;
	k->page_size = sysconf(_SC_PAGESIZE);

	/* Read e820 memory map from /proc/iomem */
	{
		unsigned i;
		FILE *proc_iomem;

		proc_iomem = fopen("/proc/iomem", "r");
		if(!proc_iomem) goto err1;

		for(i = 0; i < E820MAX; ) {
			char *line;
			size_t start, end;
			int type;

			line = s_getline(proc_iomem);
			if(!line) break;

			if(line[0] == ' ') goto next_line;
			if(sscanf(line, "%zx-%zx : %n", &start, &end, &type) != 2) goto next_line;

			k->e820[i].start = start;
			if(start > end) goto next_line;
			k->e820[i].len = end - start;

			k->e820[i].type =
				!strcmp(line + type, "System RAM") ? KEXEC_RAM :
				!strcmp(line + type, "reserved") ? KEXEC_RESERVED :
				!strcmp(line + type, "ACPI Tables") ? KEXEC_ACPI :
				!strcmp(line + type, "ACPI Non-volatile Storage") ? KEXEC_NVS :
				0;

			if(!k->e820[i].type) goto next_line;

			++i;
			next_line: free(line);
		}
		k->n_e820 = i;

		fclose(proc_iomem);
	}

	*k_out = k;
	return 0;

	freeing: err1: free(k);
	err0: return -1;
}
int kexec_new(struct kexec **k_out) { return kexec_newfree(k_out, NULL); }
void kexec_free(struct kexec *k) { if(k) kexec_newfree(NULL, k); }

static unsigned long round_up(unsigned long n, unsigned b) { return ((n - 1) / b + 1) * b; }

static int find_hole(kexec_addr *addr_out, struct kexec *k, size_t sz)
{
	unsigned i;
	sz = round_up(sz, k->page_size);
	for(i = 0; i < k->n_e820; ++i) {
		unsigned j;
		kexec_addr start;

		if(k->e820[i].type != KEXEC_RAM) continue;
		start = round_up(k->e820[i].start, k->page_size);

		/* Check that the segment has enough space. */
		try_this_address: if(start + sz > k->e820[i].start + k->e820[i].len) continue;

		/* Look for collisions with other kexec_segments. */
		for(j = 0; j < k->n_segs; ++j) {
			if(!(start + sz <= k->segs[j].mem ||
				start >= k->segs[j].mem + k->segs[j].memsz)) {
				/* There is a collision. Maybe still enough
				 * space left in this segment? */
				start = round_up(k->segs[j].mem + k->segs[j].memsz, k->page_size);
				goto try_this_address;
			}
		}
		/* It fit and no collisions, yay! */
		*addr_out = start;
		break;
	}
	if(i == k->n_e820) return -1;

	return 0;
}

static int actual_add(struct kexec *k, void *buf, size_t buf_sz, kexec_addr start)
{
	if(k->n_segs == KEXEC_SEGMENT_MAX) return -1;
	k->segs[k->n_segs].buf = buf;
	k->segs[k->n_segs].bufsz = buf_sz;
	k->segs[k->n_segs].mem = start;
	k->segs[k->n_segs].memsz = round_up(buf_sz, k->page_size);
	++k->n_segs;
	return 0;
}

int kexec_add_segment(struct kexec *k, void *buf, size_t buf_sz, kexec_addr *start_out)
{
	unsigned i;
	kexec_addr start;

	if(find_hole(&start, k, buf_sz) < 0) return -1;
	if(actual_add(k, buf, buf_sz, start) < 0) return -1;
	*start_out = start;
	return 0;
}

int kexec_add_segment_at(struct kexec *k, void *buf, size_t sz, kexec_addr start)
{
	unsigned i;

	sz = round_up(sz, k->page_size);
	for(i = 0; i < k->n_e820; ++i) {
		if(k->e820[i].type != KEXEC_RAM) continue;
		if(k->e820[i].start <= start && k->e820[i].start + k->e820[i].len >= start + sz) {
			unsigned j;
			for(j = 0; j < k->n_segs; ++j) {
				if(!(start + sz < k->segs[j].mem ||
					start >= k->segs[j].mem + k->segs[j].memsz)) return -1;
			}
			return actual_add(k, buf, sz, start);
		}
	}
	return -1;
}

int kexec_e820_foreach(struct kexec *k, kexec_e820_f *f, void *user)
{
	unsigned i;
	for(i = 0; i < k->n_e820; ++i) {
		int retv;
		retv = f(user, &k->e820[i]);
		if(retv) return retv;
	}
	return 0;
}

/* Syscall stuff */

static int kernel_kexec_load(
	unsigned long entry,
	unsigned long n_segments,
	struct kexec_segment *segments)
{
	/* KEXEC_ARCH_DEFAULT = 0 */
	int a= syscall(__NR_kexec_load, entry, n_segments, segments, (unsigned long)0);
#include <errno.h>
if(a) printf("err: %s\n",strerror(errno));
	return a;
}

static int reboot(void)
{
	int status;
	pid_t pid;

	pid = fork();
	if(pid == 0) {
		int devnull;
		/* We don't want to inflict the "System going down" message on
		 * an unsuspecting user. */
		devnull = open("/dev/null", O_WRONLY);
		dup2(devnull, 1);
		dup2(devnull, 2);

#		define X(path) execl(path "reboot", "reboot", NULL)
		X("/usr/bin/");
		X("/sbin/");
		X("/bin/");
#		undef X
		exit(1);
	}

	if(pid < 0) return -1;

	if(waitpid(pid, &status, 0) < 0) return -1;
	if(WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
	return -1;
}

int kexec_boot(struct kexec *k, kexec_addr entry)
{
	if(kernel_kexec_load(entry, k->n_segs, k->segs) < 0) return -1;
	if(reboot() < 0) return -1;
	return 0;
}
