// SPDX-License-Identifier: GPL-2.0-or-later
/* Binary stdin/stdout transport; diagnostics never contaminate stdout. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mtd/mtd-user.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "probe.h"

/* Separate public MTD ioctl path. It never erases, and cannot cross block 7. */
static int mtd_request(struct probe_request *r, int allow_write)
{
	struct mtd_info_user info;
	unsigned long offset;
	char name[32];
	FILE *file;
	int fd, ret = -EINVAL;
	uint64_t start;

	if (r->page < 448 || r->page >= 512 || r->column > 2048 ||
	    r->length > 2048 - r->column || r->chunk > 128 || r->flags > 2)
		return -EINVAL;
	if (r->op == 23 && !allow_write)
		return -EROFS;
	file = fopen("/sys/class/mtd/mtd3/offset", "r");
	if (!file)
		return -errno;
	ret = fscanf(file, "%lu", &offset);
	fclose(file);
	if (ret != 1 || offset != 0xc0000)
		return -ENODEV;
	file = fopen("/sys/class/mtd/mtd3/name", "r");
	if (!file)
		return -errno;
	ret = fscanf(file, "%31s", name);
	fclose(file);
	if (ret != 1 || strcmp(name, "spl-3"))
		return -ENODEV;
	fd = open("/dev/mtd3", r->op == 23 ? O_RDWR : O_RDONLY);
	if (fd < 0)
		return -errno;
	if (ioctl(fd, MEMGETINFO, &info) || info.type != MTD_NANDFLASH ||
	    info.size != 0x40000 || info.erasesize != 0x20000 ||
	    info.writesize != 2048 || info.oobsize != 128) {
		close(fd);
		return -ENODEV;
	}
	start = (uint64_t)r->page * 2048 - offset + r->column;
	if (r->op == 23) {
		struct mtd_write_req wr = {
			.start = start, .len = r->length, .ooblen = r->chunk,
			.usr_data = r->length ? (uintptr_t)r->data : 0,
			.usr_oob = r->chunk ? (uintptr_t)(r->data + 2048) : 0,
			.mode = r->flags,
		};
		ret = ioctl(fd, MEMWRITE, &wr) ? -errno : 0;
	} else {
		struct mtd_read_req rd = {
			.start = start, .len = r->length, .ooblen = r->chunk,
			.usr_data = r->length ? (uintptr_t)r->data : 0,
			.usr_oob = r->chunk ? (uintptr_t)(r->data + 2048) : 0,
			.mode = r->flags,
		};
		ret = ioctl(fd, MEMREAD, &rd) ? -errno : 0;
		r->regs[0] = rd.ecc_stats.corrected_bitflips;
		r->regs[1] = rd.ecc_stats.uncorrectable_errors;
		r->regs[2] = rd.ecc_stats.max_bitflips;
		r->regs[3] = rd.len;
		r->regs[4] = rd.ooblen;
	}
	close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	struct probe_request request;
	int fd = open("/dev/nfc_probe", O_RDWR);
	int ret = 1;
	int allow_write = argc == 2 && !strcmp(argv[1], "--allow-mtd-write");

	if (argc != 1 && !allow_write)
		return 2;

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
		if (request.op == 22 || request.op == 23) {
			request.result = mtd_request(&request, allow_write);
		} else if (ioctl(fd, PROBE_IOC, &request)) {
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
