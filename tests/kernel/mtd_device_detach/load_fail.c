/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd, ret, error;

	if (argc != 2)
		return 1;
	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		return 1;
	ret = syscall(SYS_finit_module, fd, "fail_init=1", 0);
	error = errno;
	close(fd);
	if (ret != -1 || error != ECANCELED) {
		fprintf(stderr, "initialization result %d, errno %d\n", ret, error);
		return 1;
	}
	return 0;
}
