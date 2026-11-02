// SPDX-License-Identifier: GPL-2.0
/*
 * A minimal character device that stores the most recent write in a fixed
 * kernel buffer. It is intentionally small: no ioctl, poll, mmap, or class
 * subsystem is included.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "char_echo"
#define BUFFER_SIZE 256

struct char_echo_file {
	size_t read_pos;
};

static dev_t char_echo_dev;
static struct cdev char_echo_cdev;
static char *char_echo_buffer;
static size_t char_echo_length;
static DEFINE_MUTEX(char_echo_lock);

static int char_echo_open(struct inode *inode, struct file *file)
{
	struct char_echo_file *state;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	file->private_data = state;
	pr_info("open: pid=%d comm=%s\n", current->pid, current->comm);
	return 0;
}

static int char_echo_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	pr_info("release: pid=%d comm=%s\n", current->pid, current->comm);
	return 0;
}

static ssize_t char_echo_read(struct file *file, char __user *user_buffer,
			      size_t count, loff_t *offset)
{
	struct char_echo_file *state = file->private_data;
	size_t available;
	size_t to_copy;
	ssize_t result;

	mutex_lock(&char_echo_lock);
	if (state->read_pos >= char_echo_length) {
		mutex_unlock(&char_echo_lock);
		return 0;
	}

	available = char_echo_length - state->read_pos;
	to_copy = min(count, available);
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

static ssize_t char_echo_write(struct file *file, const char __user *user_buffer,
			       size_t count, loff_t *offset)
{
	struct char_echo_file *state = file->private_data;
	char *temporary_buffer;
	size_t to_copy;

	if (count == 0)
		return 0;

	/*
	 * Reserve one byte for a trailing NUL. This lets the same buffer be
	 * printed safely from kernel code while the read path still returns the
	 * exact number of bytes supplied by the user.
	 */
	to_copy = min(count, (size_t)(BUFFER_SIZE - 1));
	temporary_buffer = kmalloc(to_copy, GFP_KERNEL);
	if (!temporary_buffer)
		return -ENOMEM;

	if (copy_from_user(temporary_buffer, user_buffer, to_copy)) {
		kfree(temporary_buffer);
		return -EFAULT;
	}

	mutex_lock(&char_echo_lock);
	memcpy(char_echo_buffer, temporary_buffer, to_copy);
	char_echo_buffer[to_copy] = '\0';
	char_echo_length = to_copy;
	state->read_pos = 0;
	mutex_unlock(&char_echo_lock);

	kfree(temporary_buffer);
	return to_copy;
}

static const struct file_operations char_echo_fops = {
	.owner = THIS_MODULE,
	.open = char_echo_open,
	.release = char_echo_release,
	.read = char_echo_read,
	.write = char_echo_write,
	.llseek = no_llseek,
};

static int __init char_echo_init(void)
{
	int result;

	char_echo_buffer = kzalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!char_echo_buffer)
		return -ENOMEM;

	result = alloc_chrdev_region(&char_echo_dev, 0, 1, DEVICE_NAME);
	if (result)
		goto free_buffer;

	cdev_init(&char_echo_cdev, &char_echo_fops);
	char_echo_cdev.owner = THIS_MODULE;
	result = cdev_add(&char_echo_cdev, char_echo_dev, 1);
	if (result)
		goto unregister_region;

	pr_info("loaded: major=%u minor=%u\n", MAJOR(char_echo_dev),
		MINOR(char_echo_dev));
	return 0;

unregister_region:
	unregister_chrdev_region(char_echo_dev, 1);
free_buffer:
	kfree(char_echo_buffer);
	char_echo_buffer = NULL;
	return result;
}

static void __exit char_echo_exit(void)
{
	cdev_del(&char_echo_cdev);
	unregister_chrdev_region(char_echo_dev, 1);
	kfree(char_echo_buffer);
	char_echo_buffer = NULL;
	pr_info("unloaded\n");
}

module_init(char_echo_init);
module_exit(char_echo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Study Lab");
MODULE_DESCRIPTION("Minimal character device with open/read/write/release");
MODULE_VERSION("1.0");
