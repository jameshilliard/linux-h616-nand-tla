// SPDX-License-Identifier: GPL-2.0-or-later
/* Bounded, RAM-only competing load. No files, devices or network accesses. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
	int status, failed = 0;

	for (unsigned int worker = 0; worker < 3; worker++) {
		pid_t pid = fork();

		if (pid < 0)
			return 1;
		if (!pid) {
			struct timespec start, now;
			const size_t words = 4 * 1024 * 1024;
			volatile uint32_t *memory = malloc(words * sizeof(*memory));
			unsigned int pass = 0;

			if (!memory)
				_exit(1);
			clock_gettime(CLOCK_MONOTONIC, &start);
			do {
				for (size_t i = 0; i < words; i++)
					memory[i] = (uint32_t)i ^ pass;
				for (size_t i = 0; i < words; i++)
					if (memory[i] != ((uint32_t)i ^ pass))
						_exit(2);
				pass++;
				clock_gettime(CLOCK_MONOTONIC, &now);
			} while (now.tv_sec - start.tv_sec < 120);
			fprintf(stderr, "RAM load worker %u: %u verified passes\n", worker, pass);
			free((void *)memory);
			_exit(0);
		}
	}
	while (wait(&status) > 0)
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			failed = 1;
	return failed;
}
