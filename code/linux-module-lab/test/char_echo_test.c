// SPDX-License-Identifier: GPL-2.0
/*
 * char_echo 的用户态测试程序。
 *
 * 本程序使用 POSIX/Linux 文件接口 open/read/write/close。这些 libc 函数最终
 * 发起 read(2)/write(2)/close(2) 等系统调用，由 VFS 分派到字符设备的
 * file_operations 回调。测试不直接调用内核模块里的 C 函数。
 */

/* errno 及相关错误码：文件接口失败后，perror() 会用 errno 生成错误信息。 */
#include <errno.h>
/* open() 和 O_RDONLY/O_WRONLY 等打开标志。 */
#include <fcntl.h>
/* fprintf()、puts()、perror() 等标准输入输出函数。 */
#include <stdio.h>
/* exit()、EXIT_SUCCESS、EXIT_FAILURE 等进程退出接口。 */
#include <stdlib.h>
/* strlen()、memset()、memcmp() 等 C 字符串/内存操作函数。 */
#include <string.h>
/* POSIX 接口 read()/write()/close() 和 ssize_t 类型。 */
#include <unistd.h>

/* 驱动注册后由 run_test.sh 使用 mknod 创建的用户态设备节点。 */
#define DEVICE_PATH "/dev/char_echo"

/* 测试接收缓冲区大小，必须大于驱动最多保存的 255 字节。 */
#define USER_BUFFER_SIZE 512

/* 统一的失败处理：perror 打印消息和 errno 对应原因，然后以非零状态退出。 */
static void fail(const char *message)
{
	/* libc/POSIX 接口：perror() 读取 errno 并输出系统错误描述。 */
	perror(message);

	/* libc 接口：exit(EXIT_FAILURE) 结束测试进程并返回失败状态。 */
	exit(EXIT_FAILURE);
}

/*
 * 写入一段数据并检查驱动实际接受的字节数。
 * 成功返回后关闭 fd，从而也覆盖驱动的 release 回调。
 */
static void write_and_check(const char *payload, size_t expected_length)
{
	ssize_t written;

	/*
	 * Linux/POSIX 接口：open(path, O_WRONLY)。
	 * 以只写方式打开设备；进入内核后先经过 VFS，再调用驱动的 open 回调。
	 * 返回值 >= 0 是文件描述符，-1 表示失败且 errno 已设置。
	 */
	int fd = open(DEVICE_PATH, O_WRONLY);

	if (fd < 0)
		fail("open for write");

	/*
	 * libc/C 接口：strlen() 计算不含 '\0' 的字符串长度。
	 * 这里明确传入有效载荷长度，避免把字符串结束符也写入设备。
	 */
	written = write(fd, payload, strlen(payload));

	/*
	 * Linux/POSIX 接口：write(fd, buf, count)。
	 * 返回实际接受的非负字节数，-1 表示失败。设备驱动可能只接受一部分数据。
	 */
	if (written < 0)
		fail("write");
	if ((size_t)written != expected_length) {
		/* libc 接口：fprintf(stderr, ...) 输出失败详情。 */
		fprintf(stderr, "write length mismatch: got %zd, expected %zu\n",
			written, expected_length);
		exit(EXIT_FAILURE);
	}

	/*
	 * Linux/POSIX 接口：close(fd)。
	 * 释放文件描述符；关闭最后一个引用时，VFS 调用驱动的 release 回调。
	 * 返回 0 表示成功，-1 表示失败。
	 */
	if (close(fd) < 0)
		fail("close after write");
}

/*
 * 重新打开设备，按小片段循环读取，并验证完整内容与长度。
 * 每次 read 都可能调用一次驱动的 read 回调，因此能覆盖多次读取与偏移推进。
 */
static void read_and_check(const char *expected, size_t expected_length)
{
	char actual[USER_BUFFER_SIZE + 1];
	size_t total = 0;
	ssize_t amount;

	/*
	 * Linux/POSIX 接口：open(path, O_RDONLY)。
	 * 每次读取重新 open，会得到 VFS 的 opened file 和驱动私有状态。
	 */
	int fd = open(DEVICE_PATH, O_RDONLY);

	if (fd < 0)
		fail("open for read");

	/*
	 * Linux/POSIX 接口：read(fd, buf, count)。
	 * 返回值大于 0 表示读到字节数，0 表示当前内容读完，-1 表示失败。
	 * 驱动每次推进本次打开独立的 read_pos。
	 */
	while ((amount = read(fd, actual + total, 16)) > 0) {
		total += (size_t)amount;
		if (total > USER_BUFFER_SIZE) {
			fprintf(stderr, "read overflowed the test buffer\n");
			exit(EXIT_FAILURE);
		}
	}
	if (amount < 0)
		fail("read");

	/* Linux/POSIX 接口：close(fd)，结束本次读取并触发驱动 release。 */
	if (close(fd) < 0)
		fail("close after read");

	/*
	 * C 接口：memcmp() 比较实际读取内容。长度相同且内容一致才算成功。
	 * actual 没有额外添加 '\0'，因为这里按字节长度比较，不按字符串打印。
	 */
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

	/*
	 * C 接口：memset(ptr, value, size)。
	 * 初始化 301 字节测试载荷，驱动器实际保存上限为 255 字节。
	 */
	memset(truncated, 'A', sizeof(truncated));

	/* 正常短消息：完整写入后应能完整读回。 */
	write_and_check(message, strlen(message));
	read_and_check(message, strlen(message));

	/*
	 * 边界用例：写入 301 字节，当前驱动只接受前 255 字节。
	 * 读取长度也应为 255，内容应为前 255 个 'A'。
	 */
	write_and_check(truncated, 255);
	read_and_check(truncated, 255);

	/* libc 接口：puts() 输出成功标记并自动追加换行。 */
	puts("char_echo_test: PASS");

	/* libc 接口：EXIT_SUCCESS 作为进程成功退出状态。 */
	return EXIT_SUCCESS;
}
