## 安装Linux环境
使用VirtualBox + UbuntuServer 26.04 LTS作为开发环境。
1. 首先安装 C/C++开发工具链与Linux内核模块管理工具集：
`sudo apt-get install build-essential kmod`
2. 安装对应Linux版本的头文件
```shell
sudo apt-get update
apt-cache search linux-headers-`uname -r`    //查找对应版本的头文件是否存在

sudo apt-get install linux-headers-`uname -r` //安装头文件
```
3. 编写KMD Hello World
```c
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int __init hello_init(void)//__init 链接器属性，将该函数放在.init.text段
{
	pr_info("hello_module: loaded***\n");
	return 0;
}

static void __exit hello_exit(void)//__exit 链接器属性，将该函数放在.exit.text段
{
	pr_info("hello_module: unloaded+++\n");
}

module_init(hello_init);//告诉模块加载器：加载模块时应调用 hello_init
module_exit(hello_exit);//告诉卸载路径：执行 rmmod 时应调用 hello_exit

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Lab");
MODULE_DESCRIPTION("Minimal Linux kernel module for learning module load and unload");
MODULE_VERSION("1.0");
```
对应的makefile
```makefile
obj-m += hello.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```
随后运行make编译，如果一切顺利，就会编译生成一个Kernel Module hello.ko，可以通过`mdinfo hello.ko`查看信息。
运行 `sudo insmod hello.ko`加载，随后运行`lsmod | grep hello` 就会看到你的内核模块信息。
得到信息如下：
```
Module      Size    Used by
hello      12288    0
```
size 表示模块当前占用的内核内存大小，单位是字节，约等于 12 KiB。它不是 hello.ko 文件本身的大小，通常会按内存页对齐并包含模块代码、数据等运行时布局。
used by : 模块当前的引用计数，表示现在没有其他模块或内核组件使用它，因此可以卸载。
4. 加载和卸载内核模块
运行如下命令，加载、卸载内核模块：
```shell
sudo insmod hello.ko //加载
sudo rmmode hello //卸载
sudo dmesg | tail -10 //打印内核日志
```
在模块加载，卸载时会被分别调用模块的注册到 module_init 和 module_exit的函数，通过内核日志就可以看到模块加载卸载时的打印：
```
[ 5612.840316] hello_module: loaded***
[ 5626.986678] hello_module: unloaded+++
```
