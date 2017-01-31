/* Simple mount wrapper that will find a device's existing mount point
 * if it can't be mounted otherwise. */
struct smount;
/* Mount the given filesystem somewhere. mountpoint_out is the mount point,
 * without trailing slash, until you call smount_free. */
int smount_new(struct smount **out, const char **mountpoint_out, const char *device);
/* As above but using UUID rather than devname. */
int smount_new_from_uuid(struct smount **out, const char **mountpoint_out, const char *uuid);

void smount_free(struct smount *m);
