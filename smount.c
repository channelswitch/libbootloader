#include "smount.h"
#include <blkid.h>
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CACHE_DIR "/var/cache/libbootloader"

/* Should we change /etc/mtab? I think no, we don't use the mount command, it's
 * easier and getting the lock file might block or be tedious to program. */

#if 0
# if defined(MNT_MNTTAB)
#  define MNTTAB_FILE MNT_MNTTAB
# elif defined(MNTTABNAME)
#  define MNTTAB_FILE MNTTABNAME
# else
#  define MNTTAB_FILE "/etc/mnttab"
# endif
#else
# define MNTTAB_FILE "/proc/mounts"
#endif

struct smount {
	char buf[512];
	const char *path_to_umount;
};

static int smount(struct smount **out, const char **mp_out, const char *dev)
{
	struct smount *m;
	char *filesystem;
	const char *mountpoint = NULL;

	if(*out) goto freeing;
	m = malloc(sizeof *m);
	if(!m) goto err0;
	m->path_to_umount = NULL;

	/* Try to mount the device at /var/cache/libbootloader/XXXXX */
	filesystem = blkid_get_tag_value(NULL, "TYPE", dev);
	if(!filesystem) goto err0;
	strcpy(m->buf, CACHE_DIR);
	mkdir(m->buf, 0700);
	strcat(m->buf, "/XXXXXX");
	if(!mkdtemp(m->buf)) goto err1;
	if(mount(dev, m->buf, filesystem, 0, NULL) < 0) {
		rmdir(m->buf);

		if(errno == EBUSY) {
			/* Is the device already mounted? (This shouldn't
			 * normally be a problem but for some reason NTFS
			 * can only be mounted once.) */
			FILE *mtab;
			mtab = setmntent(MNTTAB_FILE, "r");
			if(!mtab) goto mtaberr;
			while(1) {
				struct mntent mntent;
				if(!getmntent_r(mtab, &mntent, m->buf, sizeof m->buf)) break;
				if(!strcmp(mntent.mnt_fsname, dev)) {
					/* The device is mounted, use that
					 * mountpoint instead. */
					mountpoint = mntent.mnt_dir;
					break;
				}
			}
			endmntent(mtab);
			mtaberr:;
		}
		else goto err1;
	}
	else m->path_to_umount = mountpoint = m->buf;
	if(!mountpoint) goto err1;

	free(filesystem);
	*out = m;
	*mp_out = mountpoint;
	return 0;

	freeing: m = *out;
	err3: if(m->path_to_umount) umount(m->path_to_umount);
	err2: if(m->path_to_umount) rmdir(m->path_to_umount);
	err1: if(!*out) free(filesystem);
	err0: return -1;
}

int smount_new(struct smount **out, const char **mountpoint_out, const char *device)
{
	*out = NULL;
	return smount(out, mountpoint_out, device);
}

int smount_new_from_uuid(struct smount **out, const char **mountpoint_out, const char *uuid)
{
	int err;
	char *device;
	device = blkid_evaluate_tag("UUID", uuid, NULL);
	if(!device) return -1;
	err = smount_new(out, mountpoint_out, device);
	free(device);
	return err;
}

void smount_free(struct smount *m)
{
	smount(&m, NULL, NULL);
}
