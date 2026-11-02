# 本 Makefile 只编译用户态测试程序，不依赖 Linux 内核构建树。

# C 编译器；允许调用者通过 make CC=clang 覆盖。
CC ?= cc

# 测试可执行文件名。
TARGET := char_echo_test

# 严格警告并开启优化，便于在编译阶段发现类型和格式错误。
CFLAGS ?= -Wall -Wextra -Werror -O2

.PHONY: all clean

all: $(TARGET)

# 只把测试源码编译成用户态 ELF 可执行文件并通过 libc 链接系统调用封装。
$(TARGET): char_echo_test.c
	$(CC) $(CFLAGS) -o $@ $<

# 删除测试构建产物。
clean:
	$(RM) $(TARGET)
