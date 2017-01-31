/* Internal interface of enumerate.c */
struct bootloader_enumerate;

struct enumerate_target {
	/* A function to free this struct. May be called before a target
	 * becomes unavailable if the application exits. You are responsible
	 * for freeing the string and closing the fd. */
	void (*free)(struct enumerate_target *self);

	/* This function is called when fd becomes readable. Return 1 to
	 * signal that the target has become unavailable. */
	unsigned (*event)(struct enumerate_target *self);

	/* Called every time a device is removed. Return 1 as above. */
	unsigned (*on_remove)(struct enumerate_target *self, const char *syspath);

	char *cmd;
	int fd;
	void *data;
};

void enumerate_add_target(struct bootloader_enumerate *e, struct enumerate_target *target, const char *suggested_name);
