/**
 * @file	target.h
 * @author	Roland Stenholm
 * @version	0.1
 * TODO license
 */

/**
 * Identifies a boot target. When it has been loaded, will contain all data
 * neccesary to boot it.
 */
struct bootloader_target;

/**
 * Begin loading a boot target. It can't be booted until it has been loaded.
 * TODO specify the format of 'command'.
 *
 * @param	out Target output.
 * @param	command String specifying the target.
 * @return	Negative on error. (You can also get errors later on
 *		in the loading process.)
 */
int bootloader_target_load(struct bootloader_target **out, const char *command);

/**
 * Free a boot target. Probably only useful if the reboot is aborted by the user
 * or fails.
 *
 * @param	t Target.
 */
void bootloader_target_free(struct bootloader_target *t);

/**
 * When the fd returned by this function becomes readable, call
 * bootloader_target_get_progress() to continue loading.
 *
 * @param	t Target.
 * @return	The file descriptor.
 */
int bootloader_target_get_fd(struct bootloader_target *t);

/**
 * Process any available data on the internal fds, and get the progress of the
 * load.
 *
 * @param	t Target.
 * @return	Negative on error (also means the load has been aborted) or a
 *		progress number between 0 and 1000, where 1000 means fully
 *		loaded and ready to be booted with bootloader_target_boot().
 */
int bootloader_target_get_progress(struct bootloader_target *t);

/**
 * Begin rebooting into the target. The system will probably take some time to
 * shut down. You can call bootloader_target_free() after this.
 *
 * @param	t Target.
 * @return	Negative number in case of error. 0 on success, meaning the
		other OS will be loaded soon.
 */
int bootloader_target_boot(struct bootloader_target *t);

