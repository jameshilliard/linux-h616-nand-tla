// SPDX-License-Identifier: GPL-2.0-or-later
/* Binary stdin/stdout transport; diagnostics never contaminate stdout. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "probe.h"

int main(void)
{
	struct probe_request request;
	int fd = open("/dev/nfc_probe", O_RDWR);
	int ret = 1;

	if (fd < 0) {
		perror("open");
		return 1;
	}
	for (;;) {
		size_t n = fread(&request, 1, sizeof(request), stdin);

		if (!n && feof(stdin))
			break;
		if (n != sizeof(request)) {
			fputs("short request\n", stderr);
			goto out;
		}
		if (ioctl(fd, PROBE_IOC, &request)) {
			perror("ioctl");
			goto out;
		}
		if (fwrite(&request, 1, sizeof(request), stdout) != sizeof(request) ||
		    fflush(stdout)) {
			perror("stdout");
			goto out;
		}
	}
	ret = 0;
out:
	close(fd);
	return ret;
}
