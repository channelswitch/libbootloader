/*
 * kexec.h
 *
 * Functions to perform kexec_load and reboot, and to get arch-specific
 * information neccesary for booting (like the e820 memory map).
 */

/* For size_t */
#include <stddef.h>
/* Pointer to/length of physical memory */
typedef unsigned long kexec_addr;

struct kexec;
int kexec_new(struct kexec **kexec_ctx_out);
void kexec_free(struct kexec *kexec_ctx);

/* Add segment to be loaded by kexec. The buffer will not actually be copied
 * until kexec_boot is called. */
int kexec_add_segment(struct kexec *k, void *buf, size_t buf_sz, kexec_addr *start_out);
int kexec_add_segment_at(struct kexec *k, void *buf, size_t buf_sz, kexec_addr start);

/* Load OS and start rebooting. */
int kexec_boot(struct kexec *k, kexec_addr entry);

struct kexec_e820 {
	kexec_addr start, len;
	enum kexec_e820_type { KEXEC_RAM = 1, KEXEC_RESERVED, KEXEC_ACPI, KEXEC_NVS } type;
};
typedef int kexec_e820_f(void *user, const struct kexec_e820 *s);
/* Call the callback for each e820 segment. If the callback returns nonzero,
 * abort and return that number. */
int kexec_e820_foreach(struct kexec *k, kexec_e820_f *f, void *user);
