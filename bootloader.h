/**
 * @file	bootloader.h
 * @author	Roland Stenholm
 * @version	0.1
 * TODO license
 *
 * @section	DESCRIPTION
 * This library boots other operating systems via the use of the kexec system
 * call.
 *
 * The struct bootloader_enumerate enumerates known boot targets. It gives you
 * one string per target, which you can then pass to bootloader_target_load().
 * The list is not exhaustive, and you don't have to use it at all if you
 * already know what you want to boot.
 *
 * Such a string for one version of Ubuntu is <tt>"linux /dev/sda1 /boot/vmlinuz-3.0.0-21-generic root=UUID=blablabla ro crashkernel=384M-2G:64M,2G-:128M quiet splash vt.handoff=7 initrd=/boot/initrd.img-3.0.0-21-generic"</tt>.
 *
 * The second part of the library deals with actually loading targets.
 * 
 * First, load the target with bootloader_target_load() and
 * bootloader_target_get_progress(). After that has succeeded (if it does),
 * start rebooting with bootloader_target_boot(). That invokes
 * <tt>/sbin/reboot</tt>, which will stop services and then reboot to the loaded
 * target. Wait for init to shut you down if you want, but you can call
 * bootloader_target_free() immediately.
 *
 * Meaningful use of this library requires root privileges.
 */
#pragma once
#ifndef LIBBLDR_H
#define LIBBLDR_H

#include "enumerate.h"
#include "target.h"

#endif
