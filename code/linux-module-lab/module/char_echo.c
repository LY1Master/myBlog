// SPDX-License-Identifier: GPL-2.0
/*
 * 最小字符设备示例：保存最近一次写入的数据，并支持分多次读取。
 *
 * 本示例刻意不实现 ioctl、poll、mmap 或 class/device_create。用户态通过
 * /dev/char_echo 发起 open/read/write/close，VFS 根据注册的 cdev 找到
 * 下面的 file_operations 回调。
 */

/* 让本文件里的 pr_info() 自动带上模块名，例如 “char_echo: loaded”。 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * 头文件按职责分组说明。内核模块不能直接包含普通用户态头文件来调用 Linux
 * 内核接口；下面这些头文件声明了本模块使用到的内核数据结构和函数。
 */

/* struct cdev、cdev_init/cdev_add/cdev_del：
 * 把设备号范围注册到内核，并把设备操作绑定到 file_operations。 */
#include <linux/cdev.h>
/* struct file、struct inode、struct file_operations、dev_t，
 * 以及 alloc_chrdev_region/unregister_chrdev_region、MAJOR/MINOR/no_llseek：
 * 这些是字符设备注册和 VFS 文件操作接口的核心声明。 */
#include <linux/fs.h>
/* __init、__exit 段属性以及模块初始化相关声明。
 * __init 函数在初始化后可以释放，__exit 只在卸载模块时使用。 */
#include <linux/init.h>
/* pr_info()、min() 等通用内核辅助宏。
 * pr_info 把日志写入内核 ring buffer，不直接写到用户进程 stdout。 */
#include <linux/kernel.h>
/* THIS_MODULE、module_init/module_exit、MODULE_LICENSE/MODULE_AUTHOR 等：
 * 用于声明模块生命周期和模块元数据。 */
#include <linux/module.h>
/* DEFINE_MUTEX、mutex_lock、mutex_unlock：
 * 提供可睡眠的互斥锁，保护多个进程共享的内核缓冲区。 */
#include <linux/mutex.h>
/* struct task_struct 和 current 宏：
 * current 表示当前正在执行该代码的进程，可读取 pid/comm 做日志。 */
#include <linux/sched.h>
/* kzalloc、kmalloc、kfree：
 * 申请/释放内核堆内存。GFP_KERNEL 表示允许在进程上下文中睡眠等待内存。 */
#include <linux/slab.h>
/* memcpy：
 * 内核侧内存复制函数，用于把临时内核缓冲区内容复制到共享内核缓冲区。 */
#include <linux/string.h>
/* copy_to_user、copy_from_user 以及 __user 地址空间标注：
 * 负责内核态和用户态之间有检查、可处理缺页的安全数据复制。 */
#include <linux/uaccess.h>
/* -ENOMEM、-EFAULT 等标准负错误码。内核函数通常返回负 errno 表示失败。 */
#include <linux/errno.h>

/* 内核设备名，同时用于设备号申请和用户态设备节点命名。 */
#define DEVICE_NAME "char_echo"

/* 内核共享缓冲区大小。其中最后 1 字节预留给字符串结束符 '\0'。 */
#define BUFFER_SIZE 256

/*
 * 每个 open() 创建一份私有状态。read_pos 不能做成全局变量，否则两个文件
 * 描述符会互相推进对方的读取位置。
 */
struct char_echo_file {
	size_t read_pos;
};

/* 动态申请得到的设备号 (主设备号, 次设备号)。 */
static dev_t char_echo_dev;

/* cdev 把 char_echo_dev 这段设备号与 char_echo_fops 关联起来。 */
static struct cdev char_echo_cdev;

/* 所有打开本设备的文件描述符共享同一个内核缓冲区。 */
static char *char_echo_buffer;

/* 当前共享缓冲区中的有效字节数，不包含预留的 '\0'。 */
static size_t char_echo_length;

/*
 * 静态初始化互斥锁。Linux 接口：DEFINE_MUTEX(name)。
 * 锁用来保护 char_echo_buffer 和 char_echo_length，避免并发读写破坏状态。
 */
static DEFINE_MUTEX(char_echo_lock);

/*
 * VFS open 回调：用户态 open("/dev/char_echo", ...) 最终会调用到这里。
 *
 * inode 描述设备节点，file 表示本次打开产生的文件对象；本次打开私有的
 * 读偏移保存在 file->private_data 中。
 */
static int char_echo_open(struct inode *inode, struct file *file)
{
	struct char_echo_file *state;

	(void)inode; /* 本示例不需要读取 inode 中的额外信息。 */

	/*
	 * Linux 接口：kzalloc(size, GFP_KERNEL)。
	 * 分配并清零一份 open 私有状态；GFP_KERNEL 允许进程上下文睡眠等待。
	 */
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM; /* 没有足够内核内存。 */

	file->private_data = state;

	/*
	 * Linux 接口/内核对象：pr_info() 写内核日志，current->pid 和
	 * current->comm 分别读取当前进程 PID 与可执行程序名。
	 */
	pr_info("open: pid=%d comm=%s\n", current->pid, current->comm);
	return 0;
}

/*
 * VFS release 回调：用户态 close() 释放最后一个文件描述符引用时调用。
 * release 与 open 配对，负责销毁本次打开创建的私有状态。
 */
static int char_echo_release(struct inode *inode, struct file *file)
{
	(void)inode;

	/*
	 * Linux 接口：kfree(ptr)。
	 * 释放 open 中由 kzalloc 分配的状态；释放后清空 private_data。
	 */
	kfree(file->private_data);
	file->private_data = NULL;

	/* Linux 接口：pr_info() 记录 release；current 指向当前进程。 */
	pr_info("release: pid=%d comm=%s\n", current->pid, current->comm);
	return 0;
}

/*
 * VFS read 回调：用户态 read(fd, user_buffer, count) 经系统调用和 VFS
 * 分派到这里。返回实际返回给用户的字节数，0 表示当前内容已经读完。
 *
 * offset 是 VFS 提供的文件偏移指针。本示例用每文件描述符独立的 read_pos，
 * 因此不直接修改 offset。
 */
static ssize_t char_echo_read(struct file *file, char __user *user_buffer,
			      size_t count, loff_t *offset)
{
	struct char_echo_file *state = file->private_data;
	size_t available;
	size_t to_copy;
	ssize_t result;

	(void)offset;

	/*
	 * Linux 接口：mutex_lock()。
	 * 共享缓冲区与长度可能被其他进程的 write 修改，所以读取快照时必须加锁。
	 */
	mutex_lock(&char_echo_lock);
	if (state->read_pos >= char_echo_length) {
		/* Linux 接口：mutex_unlock()，所有返回路径都必须释放锁。 */
		mutex_unlock(&char_echo_lock);
		return 0;
	}

	/*
	 * Linux 接口：min(a, b)。
	 * 本次最多复制剩余可用字节数，且不能超过用户请求的 count。
	 */
	available = char_echo_length - state->read_pos;
	to_copy = min(count, available);

	/*
	 * Linux 接口：copy_to_user(dst_user, src_kernel, size)。
	 * 返回 0 表示全部复制成功；非 0 表示未能复制的字节数，转换为 -EFAULT。
	 */
	if (copy_to_user(user_buffer, char_echo_buffer + state->read_pos,
			 to_copy)) {
		result = -EFAULT;
	} else {
		state->read_pos += to_copy;
		result = to_copy;
	}
	mutex_unlock(&char_echo_lock);

	return result;
}

/*
 * VFS write 回调：用户态 write(fd, user_buffer, count) 最终调用到这里。
 * 当前实现用新数据替换旧内容，最多接受 BUFFER_SIZE - 1 字节。
 *
 * offset 含义与 read 相同；本设备允许重复读取刚写入的快照，所以不使用它。
 */
static ssize_t char_echo_write(struct file *file, const char __user *user_buffer,
			       size_t count, loff_t *offset)
{
	struct char_echo_file *state = file->private_data;
	char *temporary_buffer;
	size_t to_copy;

	(void)offset;

	if (count == 0)
		return 0;

	/* 预留 1 字节给 '\0'；读取时仍只返回用户实际写入的有效字节数。 */
	to_copy = min(count, (size_t)(BUFFER_SIZE - 1));

	/* Linux 接口：kmalloc(size, GFP_KERNEL) 分配临时内核缓冲区。 */
	temporary_buffer = kmalloc(to_copy, GFP_KERNEL);
	if (!temporary_buffer)
		return -ENOMEM;

	/*
	 * Linux 接口：copy_from_user(dst_kernel, src_user, size)。
	 * 返回 0 表示全部复制成功；非 0 表示未能复制的字节数，转换为 -EFAULT。
	 */
	if (copy_from_user(temporary_buffer, user_buffer, to_copy)) {
		/* Linux 接口：kfree() 释放失败路径上的临时缓冲区。 */
		kfree(temporary_buffer);
		return -EFAULT;
	}

	/* Linux 接口：mutex_lock() 保护共享缓冲区的替换过程。 */
	mutex_lock(&char_echo_lock);

	/*
	 * Linux 接口：memcpy(dst, src, size)。
	 * 把已经通过 copy_from_user 校验的数据复制到共享内核缓冲区。
	 */
	memcpy(char_echo_buffer, temporary_buffer, to_copy);
	char_echo_buffer[to_copy] = '\0';
	char_echo_length = to_copy;

	/* 新内容替换旧内容后，本次打开的文件描述符从新内容开头读取。 */
	state->read_pos = 0;

	/* Linux 接口：mutex_unlock()。 */
	mutex_unlock(&char_echo_lock);

	/* Linux 接口：kfree() 释放不再需要的临时缓冲区。 */
	kfree(temporary_buffer);
	return to_copy;
}

/*
 * file_operations 是 VFS 回调表，不是普通 C++/Java 接口。
 * 注册后，VFS 根据用户态调用的文件操作选择对应的回调函数。
 */
static const struct file_operations char_echo_fops = {
	/*
	 * Linux 接口：THIS_MODULE。
	 * 保存模块归属，帮助内核管理模块引用和卸载生命周期。
	 */
	.owner = THIS_MODULE,
	.open = char_echo_open,
	.release = char_echo_release,
	.read = char_echo_read,
	.write = char_echo_write,
	/*
	 * Linux 接口/回调：no_llseek。
	 * 表示不支持 lseek 随机定位；用户态对该设备执行 lseek 会失败。
	 */
	.llseek = no_llseek,
};

/*
 * 模块加载入口。__init 是 Linux 段属性宏：告诉链接器该函数只用于初始化，
 * 初始化完成后其代码所在内存可以被释放。
 */
static int __init char_echo_init(void)
{
	int result;

	/*
	 * Linux 接口：kzalloc(size, GFP_KERNEL)。
	 * 分配清零后的共享内核缓冲区。
	 */
	char_echo_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!char_echo_buffer)
		return -ENOMEM;

	/*
	 * Linux 接口：alloc_chrdev_region(&dev, first_minor, count, name)。
	 * 动态申请 1 个字符设备号，first_minor 为 0，name 显示在 /proc/devices。
	 * 返回 0 表示成功，负错误码表示失败。
	 */
	result = alloc_chrdev_region(&char_echo_dev, 0, 1, DEVICE_NAME);
	if (result)
		goto free_buffer;

	/*
	 * Linux 接口：cdev_init(cdev, fops)。
	 * 初始化 cdev 并绑定 file_operations，此时尚未加入内核注册表。
	 */
	cdev_init(&char_echo_cdev, &char_echo_fops);

	/* Linux 接口：THIS_MODULE，登记 cdev 所属模块。 */
	char_echo_cdev.owner = THIS_MODULE;

	/*
	 * Linux 接口：cdev_add(cdev, dev, count)。
	 * 把设备号范围真正注册到内核，使 VFS 能根据设备号找到这些回调。
	 */
	result = cdev_add(&char_echo_cdev, char_echo_dev, 1);
	if (result)
		goto unregister_region;

	/*
	 * Linux 接口：MAJOR(dev)、MINOR(dev) 从 dev_t 中取出主/次设备号；
	 * pr_info() 把加载成功和动态主设备号写入内核日志。
	 */
	pr_info("loaded: major=%u minor=%u\n", MAJOR(char_echo_dev),
		MINOR(char_echo_dev));
	return 0;

unregister_region:
	/*
	 * Linux 接口：unregister_chrdev_region(dev, count)。
	 * cdev_add 失败后，撤销已经申请成功的字符设备号。
	 */
	unregister_chrdev_region(char_echo_dev, 1);
free_buffer:
	/* Linux 接口：kfree() 释放共享缓冲区并清除悬空指针。 */
	kfree(char_echo_buffer);
	char_echo_buffer = NULL;
	return result;
}

/*
 * 模块卸载入口。__exit 是 Linux 段属性宏，表示该函数只服务于可卸载模块；
 * 即使误写返回值，shell 也不会取得它，但按照现代内核签名应使用 void。
 */
static void __exit char_echo_exit(void)
{
	/*
	 * Linux 接口：cdev_del(cdev)。
	 * 先从内核设备操作表中移除 cdev，阻止新的 VFS 请求进入回调。
	 */
	cdev_del(&char_echo_cdev);

	/* Linux 接口：unregister_chrdev_region()，归还动态申请的设备号。 */
	unregister_chrdev_region(char_echo_dev, 1);

	/* Linux 接口：kfree()，释放模块持有的共享缓冲区。 */
	kfree(char_echo_buffer);
	char_echo_buffer = NULL;

	/* Linux 接口：pr_info() 记录卸载日志。 */
	pr_info("unloaded\n");
}

/*
 * Linux 接口：module_init(init_fn)。
 * 把 char_echo_init 注册为 insmod/finit_module 成功时的初始化入口。
 */
module_init(char_echo_init);

/*
 * Linux 接口：module_exit(exit_fn)。
 * 把 char_echo_exit 注册为 rmmod/delete_module 时的清理入口。
 */
module_exit(char_echo_exit);

/*
 * Linux 接口：MODULE_LICENSE()。
 * 声明模块许可证；使用 GPL 程序接口或 GPL 导出符号时通常必须声明兼容许可证。
 */
MODULE_LICENSE("GPL");

/* Linux 接口：MODULE_AUTHOR() 写入 modinfo 可查看的作者元数据。 */
MODULE_AUTHOR("Study Lab");

/* Linux 接口：MODULE_DESCRIPTION() 写入模块用途描述。 */
MODULE_DESCRIPTION("Minimal character device with open/read/write/release");

/* Linux 接口：MODULE_VERSION() 写入模块自身版本号。 */
MODULE_VERSION("1.0");
