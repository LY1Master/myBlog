# 本 Makefile 只由 Linux Kbuild 构建内核模块，不编译用户态测试程序。

# Kbuild 约定：obj-m 中的目标会被构建成可加载模块（.ko）。
obj-m += char_echo.o

# 当前运行内核的构建目录，其中包含 Kbuild 入口和内核头文件。
KDIR ?= /lib/modules/$(shell uname -r)/build

# 当前模块源码目录。单独命名，避免和 shell 的 PWD 环境变量混淆。
MODULE_DIR := $(CURDIR)

.PHONY: all clean

# 进入当前内核的 Kbuild 目录，但只构建 PWD 下的外部模块。
all:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) modules

# 清理 Kbuild 在模块目录中生成的 .ko/.o/.mod/.cmd 等产物。
clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) clean
