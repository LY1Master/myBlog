#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#define BUFFER_SIZE 256
#define DEVICE_NAME "char_echo"

static dev_t char_dev;
static struct cdev char_cdev;
struct char_echo_file{
    int read_pos;
    int write_pos;
};


static char p_buffer[BUFFER_SIZE];

static int open(struct inode *inode,struct file *file){

    struct char_echo_file *state;
    state = kzalloc(sizeof(*state),GFP_KERNEL);
    if(!state){
        kfree(state);
        return -ENOMEM;
    }
    file->private_data = state;

    pr_info("open: pid=%d comm=%s\n", current->pid, current->comm);
	return 0;

}

static int release(struct inode *inode,struct file *file){

    pr_info("release --- \n");

    return 0;
}

static ssize_t read(struct file *file, char __user *user_buffer,size_t count, loff_t *offset){

    copy_to_user(user_buffer,p_buffer,count); 
    pr_info("read --- %zu\n",count);
    return 0;    
}

static ssize_t write(struct file *file, const char __user *user_buffer,size_t count, loff_t *offset){
    
    pr_info("write1 --- %zu\n",count);
    copy_from_user(p_buffer,user_buffer,count); 
    pr_info("write2 --- %zu\n",count);
    
    return 0;    
}

static const struct file_operations char_dev_ops = {

    .owner = THIS_MODULE,
    .open = open,
    .release = release,
    .read = read,
    .write = write,
};

static char* buffer;
static struct class* my_class;
static struct device* my_device;
static int __init char_echo_init(void){

    int result;

    buffer = kzalloc(BUFFER_SIZE,GFP_KERNEL);
    if(!buffer){
        return -ENOMEM;
    }

    result = alloc_chrdev_region(&char_dev,0,1,DEVICE_NAME);

    if(result != 0){
        kfree(buffer);
        buffer = NULL;
        return result;
    }

    cdev_init(&char_cdev,&char_dev_ops);

    char_cdev.owner = THIS_MODULE;

    result = cdev_add(&char_cdev,char_dev,1);
    if(result != 0){
        unregister_chrdev_region(char_dev,1);
        kfree(buffer);
        buffer = NULL;
        return result;
    }

    my_class = class_create(DEVICE_NAME);

    my_device = device_create(my_class,NULL,char_dev,NULL,DEVICE_NAME);

    pr_info("loaded: major=%u minor=%u\n", MAJOR(char_dev),MINOR(char_dev));
	return 0;

}

static void __exit char_echo_exit(void){
    device_destroy(my_class,char_dev);
    class_destroy(my_class);
    cdev_del(&char_cdev);
    unregister_chrdev_region(char_dev,1);
    kfree(buffer);
    buffer = NULL;
    pr_info("unloaded\n");
}

module_init(char_echo_init);
module_exit(char_echo_exit);

MODULE_LICENSE("GPL");

MODULE_AUTHOR("y");
MODULE_DESCRIPTION("Minimal character device with open/read/write/release");
MODULE_VERSION("1.0");