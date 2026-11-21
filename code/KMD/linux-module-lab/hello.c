// SPDX-License-Identifier: GPL-2.0
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
