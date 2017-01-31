#define _GNU_SOURCE
#include "linux.h"
#include "s.h"
#include "aio.h"
#include "kexec.h"
#include <stdlib.h>
#include <sys/epoll.h>
#include <asm/bootparam.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>

#include <stdio.h>
#include <errno.h>

#define _GNU_SOURCE
#include <unistd.h>

/* Parsing command line options */

static const char *get_word(const char *cmd, unsigned nr)
{
	cmd += strspn(cmd, " \t");
	while(nr) {
		cmd += strcspn(cmd, " \t");
		cmd += strspn(cmd, " \t");
		if(!cmd[0]) return NULL;
		--nr;
	}
	return cmd;
}

static char *get_word_2(const char *cmd, unsigned nr)
{
	const char *start;
	start = get_word(cmd, nr);
	if(!start) return NULL;
	return s_ndup(start, strcspn(start, " \t"));
}

static const char *find_arg(const char *cmd, const char *arg)
{
	const char *pos, *needle;
	size_t arglen;
	arglen = strlen(arg);
	pos = cmd;
	while(needle = strstr(pos, arg)) {
		if(pos == cmd || pos[-1] == ' ') {
			if(!needle[arglen] || needle[arglen] == ' ' || needle[arglen] == '=') return needle;
		}
	}
	return NULL;
}

static char *get_arg_value(const char *cmd, const char *arg)
{
	const char *opt;
	if(opt = find_arg(cmd, arg)) {
		size_t len;
		len = strlen(arg);
		if(opt[len] == '=') {
			const char *s;
			s = strchr(opt + len + 1, ' ');
			if(s) return s_ndup(opt + len + 1, s - (opt + len + 1));
			return s_dup(opt + len + 1);
		}
	}
	return NULL;
}

static unsigned has_opt(const char *cmd, const char *arg)
{
	return !!find_arg(cmd, arg);
}

/*
 * Getting info
 *
 * Linux boot commands look like "linux <device with kernel> <kernel file>
 * <options>" but target.c removes the "linux" at the start.
 */

char *linux_get_name(const char *cmd)
{
	char *root, *distro, *full_name, *version;
	size_t version_len;
	struct smount *mnt;
	const char *mountpoint;
	int err;
	distro = full_name = version = NULL;
	root = get_arg_value(cmd, "root");
	if(!root) {
		/* TODO may be syslinux or something */
		goto err0;
	}

	if(!strncmp(root, "UUID=", 5)) err = smount_new_from_uuid(&mnt, &mountpoint, root + 5);
	else err = smount_new(&mnt, &mountpoint, root);
	if(err < 0) goto err1;

	/* The LSB method, only supported by newer distros. */
	{
		char *lsb_release_path, *line;
		FILE *lsb_release;
		lsb_release_path = s_concat(mountpoint, "/etc/lsb-release", NULL);
		if(!lsb_release_path) goto lsb_err0;
		lsb_release = fopen(lsb_release_path, "r");
		if(!lsb_release) goto lsb_err1;

		while(!distro && (line = s_getline(lsb_release))) {
			const char *equals;
			equals = strchr(line, '=');
			if(!equals) goto lsb_line_out;
			if(!strncmp(line, "DISTRIB_DESCRIPTION", equals - line)) {
				const char *start_quote, *end_quote;
				start_quote = strchr(equals + 1, '"');
				if(!start_quote) goto lsb_line_out;
				end_quote = strchr(start_quote + 1, '"');
				if(!end_quote) goto lsb_line_out;
				distro = s_ndup(start_quote + 1, end_quote - start_quote - 2);
			}
			lsb_line_out: free(line);
		}

		fclose(lsb_release);
		lsb_err1: free(lsb_release_path);
		lsb_err0:;
	}

	/* TODO: Other ways of determining the distro? */

	/* Find kernel version info (there are often multiple ways to boot the
	 * same OS, and we want the user to be able to distinguish between
	 * them). */
	{
		/* "0x20e/2 pointer to kernel version string" */
		char *device, *filename, *path;
		struct smount *mnt;
		const char *mp;
		int err, kernel;
		unsigned char buf[2];
		err = 1;

		device = get_word_2(cmd, 0);
		if(!device) goto ver_err0;
		if(smount_new(&mnt, &mp, device) < 0) goto ver_err1;
		filename = get_word_2(cmd, 1);
		if(!filename) goto ver_err2;
		path = s_concat(mp, filename, NULL);
		if(!path) goto ver_err3;
		kernel = open(path, O_RDONLY | O_CLOEXEC);
		if(kernel < 0) goto ver_err4;

		if(lseek(kernel, 0x20e, SEEK_SET) < 0) goto ver_err5;
		if(read(kernel, buf, 2) < 0) goto ver_err5;
		if(lseek(kernel, (buf[0] | buf[1] << 8) + 0x200, SEEK_SET) < 0) goto ver_err5;

		version = s_getstr(kernel);
		if(!version) goto ver_err5;
		err = 0;

		ver_err5: close(kernel);
		ver_err4: free(path);
		ver_err3: free(filename);
		ver_err2: smount_free(mnt);
		ver_err1: free(device);
		ver_err0: if(err) goto err1;
	}

	if(distro) {
		unsigned recovery;
		recovery = has_opt(cmd, "recovery");

		full_name = s_concat(distro, version || recovery ? " (" : "", version ? "Linux " : "", version ? version : "", version && recovery ? ", " : "", recovery ? "Recovery Mode" : "", version || recovery ? ")" : "", NULL);

	}
	else {
		/* No better alternative */
		full_name = s_concat("Linux (", cmd + 5 + strspn(cmd + 5, " \t"), ")", NULL);
	}

	free(version);
	free(distro);
	smount_free(mnt);
	err1: free(root);
	err0: return full_name;
}

int linux_is_bootable(const char *cmd)
{
/*TODO*/
return 1;
}

/*
 * Actually booting a kernel
 */

/* The Linux target */

struct linux_target {
	const char *mountpoint;
	char *cmdline;
	struct smount *kernel_fs;
	struct kexec *kexec_ctx;
	int epoll_fd;

	struct aio *kernel, *initrd;
	size_t krn_full, krn_now, inrd_full, inrd_now;
};

static int load(struct linux_target *t, struct linux_target **out, const char *cmd)
{
	char *kernel_fs_devname;
	kernel_fs_devname = NULL;

	if(t) goto freeing;

	t = malloc(sizeof *t);
	if(!t) goto err0;

	t->cmdline = s_dup(get_word(cmd, 2));
	if(!t->cmdline) goto err0_5;

	kernel_fs_devname = get_word_2(cmd, 0);
	if(!kernel_fs_devname) goto err1;

	if(kexec_new(&t->kexec_ctx) < 0) goto err1_5;

	if(smount_new(&t->kernel_fs, &t->mountpoint, kernel_fs_devname) < 0) goto err2;

	/* Start loading the kernel */
	{
		int err = -1;
		char *kernel, *kernel_full_path;

		kernel = get_word_2(cmd, 1);
		if(!kernel) goto kernel_err0;

		kernel_full_path = s_concat(t->mountpoint, "/", kernel, NULL);
		if(!kernel_full_path) goto kernel_err1;

		if(aio_begin_read(&t->kernel, kernel_full_path, &t->krn_full) < 0) goto kernel_err2;

		err = 0;
		kernel_err2: t->krn_now = 0;
		free(kernel_full_path);
		kernel_err1: free(kernel);
		kernel_err0: if(err) goto err3;
	}

	/* Avoid div by 0 in linux_get_progress. */
	if(t->krn_full == 0) goto err3;

	/* Start loading the initrd if we have one */
	{
		int err;
		char *initrd, *initrd_full_path;
		err = 0;
		t->initrd = NULL;

		initrd = get_arg_value(cmd, "initrd");
		/* Note: not an error not to have an initrd. */
		if(!initrd) {
			t->inrd_full = 0;
			goto initrd_err0;
		}

		initrd_full_path = s_concat(t->mountpoint, "/", initrd, NULL);
		if(!initrd_full_path) { err = 1; goto initrd_err1; }

		if(aio_begin_read(&t->initrd, initrd_full_path, &t->inrd_full) < 0) err = 1;
		t->inrd_now = 0;

		free(initrd_full_path);
		initrd_err1: free(initrd);
		initrd_err0: if(err) goto err4;
	}

	/* Give the user an epoll fd, since we may have 2 fds to listen to */
	t->epoll_fd = epoll_create1(0);
	if(t->epoll_fd < 0) goto err5;

	{
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.u32 = 1;
		if(epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, aio_get_fd(t->kernel), &ev) < 0) goto err6;
		ev.data.u32 = 2;
		if(t->initrd) if(epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, aio_get_fd(t->initrd), &ev) < 0) goto err6;
	}

	free(kernel_fs_devname);
	*out = t;
	return 0;

	freeing: err6: close(t->epoll_fd);
	err5: aio_free(t->initrd);
	err4: aio_free(t->kernel);
	err3: smount_free(t->kernel_fs);
	err2: free(kernel_fs_devname);
	err1_5: kexec_free(t->kexec_ctx);
	err1: free(t->cmdline);
	err0_5: free(t);
	err0: return -1;
}
int linux_load(struct linux_target **out, const char *cmd) { return load(NULL, out, cmd); }
void linux_free(struct linux_target *t) { load(t, NULL, NULL); }

int linux_get_fd(struct linux_target *t)
{
	return t->epoll_fd;
}

int linux_get_progress(struct linux_target *t)
{
	struct epoll_event ev;
	int progress;

	if(epoll_wait(t->epoll_fd, &ev, 1, 0)) {
		int err;
		if(ev.data.u32 == 1) {
			err = aio_process(t->kernel, &t->krn_now, NULL);
		}
		else if(ev.data.u32 == 2) {
			err = aio_process(t->initrd, &t->inrd_now, NULL);
		}

		if(err < 0) return -1;
	}
	return aio_progress_div(t->krn_now + t->inrd_now, t->krn_full + t->inrd_full, 1000);
}

/*
 * Now for the actual booting. It's complicated.
 */

struct inrd_addr_data { kexec_addr size, max, start; };
static int inrd_addr(void *user, const struct kexec_e820 *seg)
{
	struct inrd_addr_data *data;
	data = user;
	if(seg->type == KEXEC_RAM) {
		kexec_addr a;
		if(seg->start + seg->len > data->max) a = data->max;
		else a = seg->start + seg->len;
		a -= data->size;

		if(a >= seg->start) data->start = a;
	}
	return 0;
}

static int per_each_e820(void *user, const struct kexec_e820 *seg)
{
	struct boot_params *p;
	p = user;

	p->e820_map[p->e820_entries].addr = seg->start;
	p->e820_map[p->e820_entries].size = seg->len;
	p->e820_map[p->e820_entries].type =
		seg->type == KEXEC_RAM ? E820_RAM :
		seg->type == KEXEC_RESERVED ? E820_RESERVED :
		seg->type == KEXEC_NVS ? E820_NVS :
		seg->type == KEXEC_ACPI ? KEXEC_ACPI :
		0;
	if(seg->type == KEXEC_RAM && seg->start <= 0x100000 && seg->start + seg->len > 0x100000) {
		kexec_addr mem_k;
		mem_k = (seg->start + seg->len - 0x100000) / 1024;
		p->screen_info.ext_mem_k = mem_k > 0xfc00 ? 0xfc00 : mem_k;
		p->alt_mem_k = mem_k > 0xffffffff ? 0xffffffff : mem_k;
	}
	++p->e820_entries;
	return 0;
}

int linux_boot(struct linux_target *t)
{
#	include "linux_trampoline.h"
#	include <stdint.h>
printf("linux_trampoline_code: %p\nlinux_trampoline_size: %lu\nlinux_trampoline_cmdline_offset: %lu\n",&linux_trampoline_code, linux_trampoline_size, linux_trampoline_params_offset);
	int retv = -1;
	unsigned char *bzimage;
	size_t start32, bzimage_sz;
	const char *cmdline_args;
	struct boot_params *p;
	/* Since the size of both the trampoline and the cmdline are not
	 * compile-time constants, we can't just make a struct. */
#	define TRAMPOLINE(p) ((unsigned char *)((char *)(p) + sizeof(struct boot_params)))
#	define CMDLINE(p) ((char *)(TRAMPOLINE(p) + linux_trampoline_size))
	size_t p_sz;

	bzimage = aio_get_file_data(t->kernel, &bzimage_sz);

	cmdline_args = t->cmdline;

	/* Create segment for variable data and trampoline. */
	p_sz = (size_t)CMDLINE(0) + strlen(cmdline_args) + 1;
	p = calloc(1, p_sz);
	if(!p) return -1;
	memcpy(&p->hdr, bzimage + 0x01f1, 0x0202 + *(unsigned char *)(bzimage + 0x201));
	memcpy(TRAMPOLINE(p), linux_trampoline_code, linux_trampoline_size);
	strcpy(CMDLINE(p), cmdline_args);

	/* Do regular boot protocol setup. */
	if(p->hdr.setup_sects == 0) p->hdr.setup_sects = 4;
	p->hdr.type_of_loader = 0xff;
	p->hdr.loadflags = 0;

	/* Fill in screen_info */
	{
		int dev_fb0;
		struct fb_fix_screeninfo finfo;
		struct fb_var_screeninfo vinfo;
		struct screen_info *s;
		s = &p->screen_info;

		s->orig_x = 0;
		s->orig_y = 0;
		s->orig_video_page = 0;
		s->orig_video_mode = 0;
		s->orig_video_cols = 80;
		s->orig_video_lines = 25;
		s->orig_video_ega_bx = 0;
		s->orig_video_points = 16;

		/* Default value if /dev/fb0 doesn't exist. */
		s->orig_video_isVGA = 1;

		dev_fb0 = open("/dev/fb0", O_RDONLY);
		if(dev_fb0 < 0) goto scr_err0;

		if(ioctl(dev_fb0, FBIOGET_FSCREENINFO, &finfo) < 0) goto scr_err1;
		if(ioctl(dev_fb0, FBIOGET_VSCREENINFO, &vinfo) < 0) goto scr_err1;

		if(!strcmp(finfo.id, "VESA VGA")) s->orig_video_isVGA = VIDEO_TYPE_VLFB;
		else if(!strcmp(finfo.id, "EFI VGA")) s->orig_video_isVGA = VIDEO_TYPE_EFI;
		else goto scr_err1;

		s->lfb_width = vinfo.xres;
		s->lfb_height = vinfo.yres;
		s->lfb_depth = vinfo.bits_per_pixel;
		s->lfb_base = finfo.smem_start;
		s->lfb_linelength = finfo.line_length;
		s->vesapm_seg = 0;

		s->lfb_size = (finfo.smem_len + 65535) / 65536;
		s->pages = (finfo.smem_len + 4095) / 4096;

		if(s->lfb_depth > 8) {
			s->red_pos = vinfo.red.offset;
			s->red_size = vinfo.red.length;
			s->green_pos = vinfo.green.offset;
			s->green_size = vinfo.green.length;
			s->blue_pos = vinfo.blue.offset;
			s->blue_size = vinfo.blue.length;
			s->rsvd_pos = vinfo.transp.offset;
			s->rsvd_size = vinfo.transp.length;
		}

		scr_err1: close(dev_fb0);
		scr_err0:;
	}

	kexec_e820_foreach(t->kexec_ctx, per_each_e820, p);
	if(!p->alt_mem_k) goto err0;

	start32 = (p->hdr.setup_sects + 1) * 512;

	/* Now load */
	{
		/* TODO: come up with a better way to automatically place
		 * segments, then rewrite this whole thing */

		kexec_addr p_start;

		if(p->hdr.version < 0x0200) goto err0;

		if(p->hdr.code32_start != 0x100000) {
			if(!p->hdr.relocatable_kernel) goto err0;
			if(0x100000 % p->hdr.kernel_alignment) goto err0;
			p->hdr.code32_start = 0x100000;
		}
		if(kexec_add_segment_at(t->kexec_ctx, bzimage + start32, bzimage_sz - start32, 0x100000) < 0) goto err0;

		if(kexec_add_segment(t->kexec_ctx, p, p_sz, &p_start) < 0) goto err0;
		*(uint32_t *)(TRAMPOLINE(p) + linux_trampoline_params_offset) = p_start;
		*(uint32_t *)(TRAMPOLINE(p) + linux_trampoline_kernel_offset) = 0x100000;
		p->hdr.cmd_line_ptr = (kexec_addr)CMDLINE(p_start);
		p->hdr.cmdline_size = strlen(cmdline_args);

		if(t->initrd) {
			size_t inrd_size;
			kexec_addr inrd_start, inrd_max;
			unsigned char *inrd;

			inrd = aio_get_file_data(t->initrd, &inrd_size);
			inrd_start = p->alt_mem_k * 1024 - inrd_size;
			inrd_max = p->hdr.version >= 0x0203 ? p->hdr.initrd_addr_max : 0x37ffffff;
			if(inrd_start + inrd_size >= inrd_max) inrd_start = inrd_max - inrd_size;
			/* XXX shouldn't care about page_size here :( */
			inrd_start -= inrd_start % sysconf(_SC_PAGESIZE);
			if(kexec_add_segment_at(t->kexec_ctx, inrd, inrd_size, inrd_start) < 0) goto err0;

			p->hdr.ramdisk_image = inrd_start;
			p->hdr.ramdisk_size = inrd_size;
		}

		if(kexec_boot(t->kexec_ctx, (kexec_addr)TRAMPOLINE(p_start)) < 0) goto err0;
	}

	retv = 0;

	err0: free(p);
	return retv;
}
