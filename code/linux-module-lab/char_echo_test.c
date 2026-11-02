// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/char_echo"
#define USER_BUFFER_SIZE 512

static void fail(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

static void write_and_check(const char *payload, size_t expected_length)
{
	ssize_t written;
	int fd = open(DEVICE_PATH, O_WRONLY);

	if (fd < 0)
		fail("open for write");

	written = write(fd, payload, strlen(payload));
	if (written < 0)
		fail("write");
	if ((size_t)written != expected_length) {
		fprintf(stderr, "write length mismatch: got %zd, expected %zu\n",
			written, expected_length);
		exit(EXIT_FAILURE);
	}

	if (close(fd) < 0)
		fail("close after write");
}

static void read_and_check(const char *expected, size_t expected_length)
{
	char actual[USER_BUFFER_SIZE + 1];
	size_t total = 0;
	ssize_t amount;
	int fd = open(DEVICE_PATH, O_RDONLY);

	if (fd < 0)
		fail("open for read");

	/* Small reads deliberately exercise repeated file_operations.read calls. */
	while ((amount = read(fd, actual + total, 16)) > 0) {
		total += (size_t)amount;
		if (total > USER_BUFFER_SIZE) {
			fprintf(stderr, "read overflowed the test buffer\n");
			exit(EXIT_FAILURE);
		}
	}
	if (amount < 0)
		fail("read");
	if (close(fd) < 0)
		fail("close after read");

	if (total != expected_length ||
	    memcmp(actual, expected, expected_length) != 0) {
		fprintf(stderr,
			"read mismatch: got %zu bytes, expected %zu bytes\n",
			total, expected_length);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	const char *message = "hello from userspace\n";
	char truncated[301];

	memset(truncated, 'A', sizeof(truncated));

	write_and_check(message, strlen(message));
	read_and_check(message, strlen(message));

	write_and_check(truncated, 255);
	read_and_check(truncated, 255);

	puts("char_echo_test: PASS");
	return EXIT_SUCCESS;
}
