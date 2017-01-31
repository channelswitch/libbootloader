#include "disk.h"
#include "enumerate_2.h"
#include "s.h"
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>

#include <string.h>
#include <errno.h>

/*
 * Scan a disk device for any operating systems. There are a number of reasons
 * for why we don't just jump to its boot sector:
 *
 * 1. We don't know what its BIOS device number would be (could probably guess for
 * hard drives and floppies but CDs and usb sticks would be difficult).
 * 2. The BIOS can't read every type of device.
 * 3. The BIOS often doesn't work after Linux has been loaded.
 * 4. The fact that we're not really interested in the boot menus one might get.
 */

static void disk_target_free(struct enumerate_target *t)
{
	free(t->cmd);
	free(t->data);
	free(t);
}

static unsigned on_remove(struct enumerate_target *t, const char *syspath)
{
	if(!strcmp(t->data, syspath)) return 1;
	return 0;
}

static void add_target(struct bootloader_enumerate *e, char *target, const char *syspath, const char *name)
{
	struct enumerate_target *t;

	t = malloc(sizeof *t);
	if(!t) goto err0;

	t->free = disk_target_free;
	t->on_remove = on_remove;
	t->cmd = target;
	t->fd = -1;
	t->data = s_dup(syspath);
	enumerate_add_target(e, t, name);

	return;

	err0: free(target);
}

static unsigned read_word(char **s, const char *word, unsigned is_symbol)
{
	unsigned i;
	for(i = 0; word[i]; ++i) {
		if(word[i] != (*s)[i]) return 0;
	}
	if(!is_symbol && strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", (*s)[i])) return 0;
	*s += i;
	return 1;
}

static void read_menuentry(struct bootloader_enumerate *e, const char *devfile, const char *syspath, FILE *grub_conf, const char *name)
{
	char *line, *a, *linux_cmd = NULL, *initrd = NULL, *root = NULL;

	while(line = a = s_getline(grub_conf)) {
		line += strspn(line, " \t");

		if(read_word(&line, "linux", 0)) {
			line += strspn(line, " \t");
			free(linux_cmd);
			linux_cmd = s_dup(line);
		}
		else if(read_word(&line, "initrd", 0)) {
			line += strspn(line, " \t");
			free(initrd);
			initrd = s_dup(line);
		}
		else if(read_word(&line, "search", 0)) {
			char *set;
			/* The search command is used to look for devices */
			if(!strstr(line, "--fs-uuid")) goto search_out;
			set = strstr(line, "--set");
			if(set && strcmp(set, "--set=root")) goto search_out;

			/* TODO finish this soon as someone has a system
			 * where the kernel is not on the same partition as
			 * grub.cfg. */

			search_out:;
		}
		else if(read_word(&line, "}", 1)) {
			free(a);
			break;
		}

		free(a);
	}

	if(linux_cmd) {
		char *target;
		target = s_concat("linux ", root ? root : devfile, " ", linux_cmd, initrd ? " initrd=" : "", initrd ? initrd : "", NULL);
		add_target(e, target, syspath, name);
	}

	/* Done with this menuentry */
	free(linux_cmd);
	free(initrd);
	free(root);
}

void disk_scan(struct bootloader_enumerate *e, const char *devfile, const char *syspath, unsigned is_partition)
{
	const char *mountpoint;
	struct smount *smnt;

	if(smount_new(&smnt, &mountpoint, devfile) < 0) goto err0;

	/* Look for GRUB 2 config */
	{
		static const char *grub_files[] = {
			"/boot/grub/grub.cfg",
			"/grub/grub.cfg",
		};

		unsigned i;
		FILE *grub_conf;
		for(i = 0; i < sizeof grub_files / sizeof grub_files[0]; ++i) {
			char *path;
			path = s_concat(mountpoint, grub_files[i], NULL);
			grub_conf = fopen(path, "r");
			free(path);
			if(grub_conf) break;
		}
		if(!grub_conf) goto grub_out;

		/* Read a GRUB 2 config file (more like skim, really) */
		{
			char *line, *a;
			while(line = a = s_getline(grub_conf)) {
				line += strspn(line, " \t");
				if(read_word(&line, "menuentry", 0)) {
					char *name;
					name = line + strcspn(line, "'\"") + 1;
					*(name + strcspn(name, "'\"")) = '\0';
					read_menuentry(e, devfile, syspath, grub_conf, name[0] == '\0' ? NULL : name);
				}
				free(a);
			}
		}

		fclose(grub_conf);
		grub_out:;
	}

	/* TODO: Look for GRUB legacy config */
	//"/boot/grub/menu.lst",
	//"/grub/menu.lst",

	/* Look for boot.ini (Windows) */
	{
		char *boot_ini_path;
		FILE *boot_ini;
		boot_ini_path = s_concat(mountpoint, "/boot.ini", NULL);
		if(!boot_ini_path) goto ini_err0;
		boot_ini = fopen(boot_ini_path, "r");
		if(!boot_ini) goto ini_err1;

		//TODO read boot.ini for windows

		fclose(boot_ini);
		ini_err1: free(boot_ini_path);
		ini_err0:;
	}

	smount_free(smnt);
	err0: return;
}
