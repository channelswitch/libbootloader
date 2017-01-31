#include "bootloader.h"
#include <stdio.h>
#include <poll.h>

#define A 0

int main(int argc, char **argv)
{
	struct bootloader_enumerate *e;
	struct bootloader_target *target;
	struct pollfd fd;
#if A
	e = bootloader_enumerate_new();
	fd = (struct pollfd){.fd = bootloader_enumerate_get_fd(e), .events = POLLIN};
	while(poll(&fd, 1, -1) > 0)
	{
		int i;
		const char *cmd, *name;
		i = bootloader_enumerate_get_change(e, &cmd, &name);
		if(i) printf("%s: %s (%s)\n\n", i == 1 ? "add" : "remove",name,cmd);
	}

	bootloader_enumerate_free(e);
#endif

#if !A
# if 0
#  define SYSTEM "linux /dev/sda1 /boot/vmlinuz-3.0.0-12-generic root=UUID=eee385e5-0420-451a-9200-3d82fafb1369 ro   crashkernel=384M-2G:64M,2G-:128M quiet splash vt.handoff=7 initrd=/boot/initrd.img-3.0.0-12-generic"
# else
//#  define SYSTEM "linux /dev/sda1 /boot/vmlinuz-2.6.32-5-amd64 root=UUID=1d850873-c2c6-40e7-a69c-248ea322b113 ro  quiet initrd=/boot/initrd.img-2.6.32-5-amd64"
#define SYSTEM "linux /dev/sda1 /boot/vmlinuz-2.6.32-5-686 root=UUID=bd371b73-59e7-4e85-95a2-c1808e57688a ro quiet initrd=/boot/initrd.img-2.6.32-5-686"
# endif
	if(bootloader_target_load(&target, SYSTEM) < 0) goto err0;

	fd.fd = bootloader_target_get_fd(target);
	fd.events = POLLIN;
	while(1) {
		int p;
		poll(&fd, 1, -1);
		p=bootloader_target_get_progress(target);
		printf("%.1f%%\n",p/10.0);
		if(p==1000)break;
	}

	bootloader_target_boot(target);
	bootloader_target_free(target);
#endif

	err0: return 0;
}
