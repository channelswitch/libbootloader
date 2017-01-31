/**
 * @file	enumerate.h
 * @author	Roland Stenholm
 * @version	0.1
 * TODO license
 *
 * @section	DESCRIPTION
 * This file contains functions to give you a list of known available boot
 * targets, subject to change in response to hardware events. The list is not
 * exhaustive, and you should allow the user to try to boot targets (command
 * strings) of their own.
 */

/**
 * This struct contains information that the library uses to determine when a
 * boot target becomes available or unavailable.
 */
struct bootloader_enumerate;

/**
 * Create an enumeration. To get the devices, call bootloader_enumerate_get_change()
 * until it returns 0.
 *
 * @return	The enumeration.
 */
struct bootloader_enumerate *bootloader_enumerate_new(void);

/**
 * Free the enumeration.
 *
 * @param	e The enumeration.
 */
void bootloader_enumerate_free(struct bootloader_enumerate *e);

/**
 * Call this function until it returns 0 to get all available boot devices.
 *
 * The devices available to boot to may change with time (USB drives inserted
 * or removed, for example). When that has happened, call this function to get
 * that change. Doesn't block.
 *
 * @param	e The library context.
 * @param	str_out	Returns the string identifying the boot target that became
 *		(un)available. Is set to %NULL whet no more devices have changed.
 *		Remains valid until the next call to this function.
 * @return	1 if the target is to be added, 2 if it's to be removed, 0 if no
 *		changes are left.
 */
int bootloader_enumerate_get_change(struct bootloader_enumerate *e, const char **cmd_out, const char **display_name_out);

/**
 * When anything happens, the fd returned by this function becomes readable. At
 * that time, call bootloader_enumerate_get_change() until it returns 0, even if you
 * don't care, because it also does other processing.
 *
 * @param	e The library context.
 * @return	The aforementioned fd.
 */
int bootloader_enumerate_get_fd(struct bootloader_enumerate *e);
