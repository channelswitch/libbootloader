struct linux_target;
int linux_is_bootable(const char *cmd);
int linux_load(struct linux_target **out, const char *cmd);
void linux_free(struct linux_target *t);
int linux_get_fd(struct linux_target *t);
int linux_get_progress(struct linux_target *t);
int linux_boot(struct linux_target *t);
char *linux_get_name(const char *cmd);
