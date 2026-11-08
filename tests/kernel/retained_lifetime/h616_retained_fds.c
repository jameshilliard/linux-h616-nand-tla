/* SPDX-License-Identifier: GPL-2.0-only */
/* Real-device read-only I/O racing with explicitly requested unbind/rebind. */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct reader {
	int fd;
	unsigned char expected[1024];
	atomic_uint reads;
	atomic_int error;
};

static int open_named(const char *name, int *index)
{
	DIR *dir = opendir("/sys/class/mtd");
	struct dirent *entry;
	char path[256], found[128], extra;
	int fd = -1, id;
	FILE *file;

	assert(dir);
	while ((entry = readdir(dir))) {
		if (sscanf(entry->d_name, "mtd%d%c", &id, &extra) != 1)
			continue;
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/name", id);
		file = fopen(path, "r");
		if (!file)
			continue;
		if (!fgets(found, sizeof(found), file))
			found[0] = 0;
		fclose(file);
		found[strcspn(found, "\n")] = 0;
		if (strcmp(found, name))
			continue;
		snprintf(path, sizeof(path), "/dev/mtd%d", id);
		fd = open(path, O_RDONLY);
		if (fd < 0 && errno == ENODEV)
			continue;
		*index = id;
		break;
	}
	closedir(dir);
	return fd;
}

static void bind_action(const char *driver, const char *device, const char *action)
{
	char path[512];
	int fd;

	snprintf(path, sizeof(path), "/sys/bus/platform/drivers/%s/%s", driver, action);
	fd = open(path, O_WRONLY);
	assert(fd >= 0);
	if (write(fd, device, strlen(device)) != (ssize_t)strlen(device)) {
		perror(action);
		abort();
	}
	assert(!close(fd));
}

static void *read_worker(void *arg)
{
	struct reader *r = arg;
	unsigned char buf[1024];

	for (;;) {
		ssize_t n = pread(r->fd, buf, sizeof(buf), 0);
		if (n < 0) {
			atomic_store(&r->error, errno);
			return NULL;
		}
		if (n != sizeof(buf) || memcmp(buf, r->expected, sizeof(buf))) {
			atomic_store(&r->error, EILSEQ);
			return NULL;
		}
		atomic_fetch_add(&r->reads, 1);
	}
}

static void check_disconnected(struct reader *r)
{
	unsigned char buf[1024];
	struct mtd_info_user info;
	struct mtd_ecc_stats stats;
	struct mtd_read_req req = {
		.len = sizeof(buf), .usr_data = (uintptr_t)buf,
		.mode = MTD_OPS_PLACE_OOB,
	};
	loff_t offset = 0;

	assert(!ioctl(r->fd, MEMGETINFO, &info));
	assert(!ioctl(r->fd, ECCGETSTATS, &stats));
	errno = 0;
	assert(pread(r->fd, buf, sizeof(buf), 0) == -1 && errno == ENODEV);
	errno = 0;
	assert(ioctl(r->fd, MEMREAD, &req) == -1 && errno == ENODEV);
	errno = 0;
	assert(ioctl(r->fd, MEMGETBADBLOCK, &offset) == -1 && errno == ENODEV);
}

int main(int argc, char **argv)
{
	unsigned int loops;

	assert(argc == 6);
	assert(!strcmp(argv[1], "sunxi_nand"));
	assert(!strcmp(argv[2], "4011000.nand-controller"));
	loops = strtoul(argv[5], NULL, 10);
	assert(loops > 0 && loops <= 1000);
	setvbuf(stdout, NULL, _IOLBF, 0);
	alarm(240);
	for (unsigned int pass = 0; pass < loops; pass++) {
		struct reader r[2] = { 0 };
		pthread_t workers[2];
		int old_id[2], new_id[2], fresh[2];
		unsigned char buf[1024];

		for (int i = 0; i < 2; i++) {
			r[i].fd = open_named(argv[3 + i], &old_id[i]);
			assert(r[i].fd >= 0);
			assert(pread(r[i].fd, r[i].expected, sizeof(buf), 0) == sizeof(buf));
			assert(!pthread_create(&workers[i], NULL, read_worker, &r[i]));
		}
		for (int i = 0; i < 2; i++)
			while (atomic_load(&r[i].reads) < 10) {
				assert(!atomic_load(&r[i].error));
				usleep(1000);
			}
		bind_action(argv[1], argv[2], "unbind");
		for (int i = 0; i < 2; i++) {
			assert(!pthread_join(workers[i], NULL));
			assert(atomic_load(&r[i].error) == ENODEV);
			check_disconnected(&r[i]);
		}
		bind_action(argv[1], argv[2], "bind");
		for (int i = 0; i < 2; i++) {
			fresh[i] = open_named(argv[3 + i], &new_id[i]);
			assert(fresh[i] >= 0 && new_id[i] != old_id[i]);
			assert(pread(fresh[i], buf, sizeof(buf), 0) == sizeof(buf));
			assert(!memcmp(buf, r[i].expected, sizeof(buf)));
			check_disconnected(&r[i]);
		}
		for (int n = 0; n < 2; n++) {
			int i = (pass + n) % 2;
			assert(!close(r[i].fd));
			assert(!close(fresh[i]));
		}
		printf("cycle=%u native_reads=%u boot_reads=%u old_ids=%d,%d new_ids=%d,%d PASS\n",
		       pass, atomic_load(&r[0].reads), atomic_load(&r[1].reads),
		       old_id[0], old_id[1], new_id[0], new_id[1]);
	}
	return 0;
}
