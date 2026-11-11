// SPDX-License-Identifier: GPL-2.0-or-later
/* Metadata and rejected I/O through an FD after its controller module unloads. */
#include <errno.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct mtd_info_user info;
	struct mtd_ecc_stats stats;
	loff_t offset = 0;
	char byte;
	int fd;

	if (argc != 2)
		return 2;
	fd = atoi(argv[1]);
	if (ioctl(fd, MEMGETINFO, &info) || info.writesize != 512 || info.oobsize != 16 ||
	    ioctl(fd, ECCGETSTATS, &stats))
		return 1;
	if (read(fd, &byte, 1) != -1 || errno != ENODEV)
		return 1;
	if (ioctl(fd, MEMGETBADBLOCK, &offset) != -1 || errno != ENODEV)
		return 1;
	puts("raw-nand-unloaded-fd: metadata accessible, I/O rejected PASS");
	return 0;
}
