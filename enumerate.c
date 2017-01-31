#define _GNU_SOURCE
#include "enumerate.h"
#include "enumerate_2.h"
#include "target_2.h"
#include "disk.h"
#include "s.h"
#include <libudev.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <poll.h>
#include <pthread.h>

/*
 * This code monitors the system for bootable devices. The monitoring itself
 * is done in a separate thread, because it needs to mount filesystems and read
 * files and generally mess about in a way that blocks for long periods of time.
 */

/* TODO:
 *
 * Config file allowing to blacklist hardware devices or add netboot targets
 * at start.
 *
 * Use Inotify to see if targets become unavailable even as the device remains.
 *
 * Use AIO with eventfd instead of a thread. (Or does mount() block too long?)
 */

/* Further on, we use epoll to wait for several types of events. A pointer to
 * a struct of this type is used as the epoll fd user data. */
typedef void event_f(struct bootloader_enumerate *e, void *user);
struct event_handle {
	event_f *f;
	void *user;
};

/* Main context for this part of the library. */
struct bootloader_enumerate {
	pthread_t monitor_thread;
	int command_pipe[2];
	char *target_cmd, *display_name;

	/* Used in the monitor thread to wait for several sources. */
	int epoll_fd;

	/* Udev stuff */
	struct udev *udev;
	struct udev_monitor *monitor;
	struct event_handle monitor_handle;

	/* Active targets, and associated information. */
	struct target_list *targets;

	/* User-visible fd. Events take the form of a character ('a' for add or
	 * 'd' for delete) followed by a null-terminated string. */
	int event_pipe[2];
};

/*
 * Targets
 */

struct target_list {
	struct target_list *next;
	/* This data is specific to the type of target and is created/handled elsewhere. */
	struct enumerate_target *target;
	struct event_handle handle;
	char *display_name;
};

static void target_event(struct bootloader_enumerate *e, struct target_list *t)
{
	if(t->target->event(t->target)) {
		/* The target has become unavailable. */
		struct target_list **i;
		char *target_str;

		/* Tell the user. */
		write(e->event_pipe[1], "d", 1);
		write(e->event_pipe[1], t->target->cmd, strlen(t->target->cmd) + 1);
		write(e->event_pipe[1], t->display_name, strlen(t->display_name) + 1);

		/* Remove it. */
		for(i = &e->targets; *i != t; i = &(*i)->next);
		*i = t->next;

		if(t->target->fd >= 0) epoll_ctl(e->epoll_fd, EPOLL_CTL_DEL, t->target->fd, NULL);
		t->target->free(t->target);
		free(t);
	}
}

static unsigned target_exists(struct bootloader_enumerate *e, const char *target)
{
	struct target_list *i;
	for(i = e->targets; i; i = i->next) if(!strcmp(target, i->target->cmd)) return 1;
	return 0;
}

void enumerate_add_target(struct bootloader_enumerate *e, struct enumerate_target *target, const char *suggested_name)
{
	struct target_list *node;
	char *name;

	if(target_exists(e, target->cmd)) goto err0;

	/* The name we get from inspecting the target OS has priority for the
	 * sake if consistency. */
	name = target_get_display_name(target->cmd);
	if(!name) name = s_dup(suggested_name);
	if(!name) goto err0;

	node = malloc(sizeof *node);
	if(!node) goto err0;
	node->display_name = name;
	node->target = target;
	node->handle.f = (event_f *)target_event;
	node->handle.user = node;

	node->next = e->targets;
	e->targets = node;

	if(target->fd >= 0) epoll_ctl(e->epoll_fd, EPOLL_CTL_ADD, target->fd, NULL);

	/* Tell the user */
	write(e->event_pipe[1], "a", 1);
	write(e->event_pipe[1], target->cmd, strlen(target->cmd) + 1);
	write(e->event_pipe[1], name, strlen(name) + 1);

	return;
	err0: target->free(target);
}

/*
 * Handling of udev_monitor events.
 */

static void scan_device(struct bootloader_enumerate *e, struct udev_device *d)
{
	const char *devtype, *devnode, *syspath;
	devtype = udev_device_get_devtype(d);
	if(!devtype) return;
	devnode = udev_device_get_devnode(d);
	syspath = udev_device_get_syspath(d);
	if(!strcmp(devtype, "partition")) disk_scan(e, devnode, syspath, 1);
	else if(!strcmp(devtype, "disk")) disk_scan(e, devnode, syspath, 0);
}

static int monitor_event(struct bootloader_enumerate *e, char **target_out, void *ignored)
{
	struct udev_device *d;
	const char *devtype;
	d = udev_monitor_receive_device(e->monitor);

	if(!strcmp(udev_device_get_action(d), "add")) {
		scan_device(e, d);
	}
	else if(!strcmp(udev_device_get_action(d), "remove")) {
		/* Remove all targets on a certain device */
		const char *syspath;
		struct target_list **i;
		syspath = udev_device_get_syspath(d);

		for(i = &e->targets; *i; i = &(*i)->next) {
			struct target_list *node;
			next: node = *i;
			if(node->target->on_remove(node->target, syspath)) {
				/* Tell the user */
				write(e->event_pipe[1], "d", 1);
				write(e->event_pipe[1], node->target->cmd, strlen(node->target->cmd) + 1);
				write(e->event_pipe[1], node->display_name, strlen(node->display_name) + 1);

				/* Free the target */
				node->target->free(node->target);
				*i = node->next;
				free(node);

				goto next;
			}
		}
	}

	udev_device_unref(d);
	return 0;
}

/*
 * Monitor thread
 */

static void *monitor(void *user)
{
	struct pollfd pfd;
	struct bootloader_enumerate *e;
	e = user;

	/* Enumerate all initial devices, scan them and put them in target_list now. */
	{
		struct udev_enumerate *enr;
		struct udev_list_entry *i;

		enr = udev_enumerate_new(e->udev);
		if(!enr) goto err6;
		if(udev_enumerate_scan_devices(enr) < 0) goto err7;

		for(i = udev_enumerate_get_list_entry(enr); i; i = udev_list_entry_get_next(i)) {
			const char *devpath;
			struct udev_device *d;
			devpath = udev_list_entry_get_name(i);
			d = udev_device_new_from_syspath(e->udev, devpath);
			scan_device(e, d);
			udev_device_unref(d);
		}

		err7: udev_enumerate_unref(enr);
	}

	err6: pfd.fd = e->command_pipe[0];
	pfd.events = POLLIN;
	while(1) {
		struct epoll_event ev;
		epoll_wait(e->epoll_fd, &ev, 1, -1);
		/* User commands have priority. */
		if(poll(&pfd, 1, 0) > 0) {
			char command;
			read(e->command_pipe[0], &command, 1);
			if(command == 'x') break;
		}
		else {
			struct event_handle *handle;
			handle = ev.data.ptr;
			handle->f(e, handle->user);
		}
	}

	return NULL;
}

/*
 * Setup/freeing, API
 */

static void dont_log(struct udev *udev, int prio, const char *file, int line, const char *fn, const char *frm, va_list args) {}

/* Initialize/free the struct bootloader_enumerate. */
static struct bootloader_enumerate *bootloader_enumerate(struct bootloader_enumerate *e)
{
	if(e) goto freeing;

	e = malloc(sizeof *e);
	if(!e) goto err0;

	e->udev = udev_new();
	if(!e->udev) goto err1;
	udev_set_log_fn(e->udev, dont_log);

	/* Make the communication pipes. */
	if(pipe2(e->event_pipe, O_CLOEXEC) < 0) goto err2;
	if(pipe2(e->command_pipe, O_NONBLOCK | O_CLOEXEC) < 0) goto err3;

	/* Create the device event monitor. */
	e->monitor = udev_monitor_new_from_netlink(e->udev, "udev");
	if(!e->monitor) goto err4;

	/* Enable monitoring. Done before enumerating, so we don't miss any events. */
	if(udev_monitor_enable_receiving(e->monitor) < 0) goto err5;

	/* Create the epoll fd. */
	e->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if(e->epoll_fd < 0) goto err5;
	{
		struct epoll_event ev = { EPOLLIN };

		/* Listen to udev_monitor */
		e->monitor_handle.f = (event_f *)monitor_event;
		ev.data.ptr = &e->monitor_handle;
		epoll_ctl(e->epoll_fd, EPOLL_CTL_ADD, udev_monitor_get_fd(e->monitor), &ev);

		/* Listen to user events (bootloader_enumerate_free) */
		ev.data.ptr = NULL;
		epoll_ctl(e->epoll_fd, EPOLL_CTL_ADD, e->command_pipe[0], &ev);
	}

	e->target_cmd = NULL;
	e->display_name = NULL;

	return e;

	freeing: if(e->target_cmd) free(e->target_cmd);
	if(e->display_name) free(e->display_name);
	err6: close(e->epoll_fd);
	err5: udev_monitor_unref(e->monitor);
	err4: close(e->command_pipe[0]);
	close(e->command_pipe[1]);
	err3: close(e->event_pipe[0]);
	close(e->event_pipe[1]);
	err2: udev_unref(e->udev);
	err1: free(e);
	err0: return NULL;
}

struct bootloader_enumerate *bootloader_enumerate_new(void) {
	struct bootloader_enumerate *e;
	e = bootloader_enumerate(NULL);
	if(!e) return NULL;

	if(pthread_create(&e->monitor_thread, NULL, monitor, e) != 0) {
		bootloader_enumerate(e);
		return NULL;
	}
	return e;
}

void bootloader_enumerate_free(struct bootloader_enumerate *e) {
	write(e->command_pipe[1], "x", 1);
	pthread_join(e->monitor_thread, NULL);
	bootloader_enumerate(e);
}

int bootloader_enumerate_get_change(struct bootloader_enumerate *e, const char **str_out, const char **display_name_out)
{
	struct pollfd fd;
	fd.fd = e->event_pipe[0];
	fd.events = POLLIN;
	if(poll(&fd, 1, 0) > 0) {
		char cmd;
		read(e->event_pipe[0], &cmd, 1);
		if(cmd == 'a' || cmd == 'd') {
			if(e->target_cmd) free(e->target_cmd);
			*str_out = e->target_cmd = s_getstr(e->event_pipe[0]);
			if(e->display_name) free(e->display_name);
			e->display_name = s_getstr(e->event_pipe[0]);
			if(display_name_out) *display_name_out = e->display_name;
			return cmd == 'a' ? 1 : 2;
		}
	}
	return 0;
}

int bootloader_enumerate_get_fd(struct bootloader_enumerate *e)
{
	return e->event_pipe[0];
}

