#include "target.h"
#include "linux.h"
#include "s.h"
#include <stdlib.h>

typedef int test_f(const char *cmd);
typedef int load_f(void **target_out, const char *dev);
typedef void free_f(void *target);
typedef int fd_f(void *target);
typedef int prgs_f(void *target);
typedef int boot_f(void *target);
typedef char *name_f(const char *cmd);
struct target_class {
	test_f *is_bootable;
	load_f *load;
	free_f *free;
	fd_f *get_fd;
	prgs_f *get_progress;
	boot_f *boot;
	name_f *name;
};

enum target_type { NONE, LINUX, TYPE_END };
static struct target_class funcs[] = {
	[LINUX] = {
		(test_f *) linux_is_bootable,
		(load_f *) linux_load,
		(free_f *) linux_free,
		(fd_f *) linux_get_fd,
		(prgs_f *) linux_get_progress,
		(boot_f *) linux_boot,
		(name_f *) linux_get_name,
	},
};

/*
 * Parsing the command string, selecting the target type.
 */

static enum target_type get_target_type(const char **str)
{
	size_t word_len;
	const char *space;
	enum target_type type = NONE;
	space = strchr(*str, ' ');
	if(!space) return NONE;
	word_len = space - *str;
	type =
		!strncmp(*str, "linux", word_len) ? LINUX :
		NONE;
	if(type != NONE) {
		*str += word_len;
		*str += strspn(*str, " \t");
	}
	return type;
}

char *target_get_display_name(const char *target)
{
	enum target_type type;
	type = get_target_type(&target);
	if(type != NONE) return funcs[type].name(target);
	return NULL;
}

struct bootloader_target {
	enum target_type type;
	void *data;
};

static void *target_newfree(int *retv, struct bootloader_target *t, const char *dev)
{
	enum target_type type;

	if(t) goto freeing;

	type = get_target_type(&dev);
	if(type == NONE) { *retv = -1; goto err0; }

	t = malloc(sizeof *t);
	if(!t) { *retv = -1; goto err0; }
	t->type = type;

	if((*retv = funcs[t->type].load(&t->data, dev)) < 0) goto err1;

	*retv = 0;
	return t;

	freeing:
	funcs[t->type].free(t->data);
	err1: free(t);
	err0: return NULL;
}

int bootloader_target_load(struct bootloader_target **out, const char *command)
{
	int retv;
	*out = target_newfree(&retv, NULL, command);
	return retv;
}
void bootloader_target_free(struct bootloader_target *t) { target_newfree(NULL, t, NULL); }

int bootloader_target_get_fd(struct bootloader_target *t)
{
	return funcs[t->type].get_fd(t->data);
}

int bootloader_target_get_progress(struct bootloader_target *t)
{
	return funcs[t->type].get_progress(t->data);
}

int bootloader_target_boot(struct bootloader_target *t)
{
	return funcs[t->type].boot(t->data);
}

