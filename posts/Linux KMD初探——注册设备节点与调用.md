# Linux KMD初探 调用hello.ko的流程
### 申请设备号
让我们先从hello.ko模块　__init函数的第一个第一个函数调用`alloc_chrdev_region` 开始:
从代码描述可以看到该函数会向内核申请一段字符设备号范围，由内核动态选择一个可用的主设备号，并把“主设备号 + 起始次设备号”返回给驱动。例如分配到的主设备号时240，参数指定的次设备号是0，那么最终输出的设备号为： `char_dev = MKDEV(240, 0)；`，同时可以获得主、次设备号：
```c
MAJOR(char_dev);  // 240
MINOR(char_dev);  // 0
```
```c
/**
 * alloc_chrdev_region() - register a range of char device numbers
 * @dev: output parameter for first assigned number
 * @baseminor: first of the requested range of minor numbers
 * @count: the number of minor numbers required
 * @name: the name of the associated device or driver
 *
 * Allocates a range of char device numbers.  The major number will be
 * chosen dynamically, and returned (along with the first minor number)
 * in @dev.  Returns zero or a negative error code.
 */
int alloc_chrdev_region(dev_t *dev,
                        unsigned baseminor,
                        unsigned count,
                        const char *name);
```
> 在 Linux 系统中，设备号（Device Number）是内核用来识别、管理和区分各种硬件设备（以及对应的驱动程序）的核心机制。
> 
> 设备号由两部分组成，类型为 dev_t（在现代 Linux 内核中通常是一个 32 位无符号整数）.
> 
> 主设备号（Major Number）• 作用：用来标识设备的类型，或者说关联哪一个驱动程序。
> 
> • 理解：内核通过主设备号来找到对应的驱动。比如，主设备号为 4 可能对应串口驱动，主设备号为 8 可能对应 SCSI 硬盘驱动。主设备号相同的设备，使用的是同一个驱动程序。
> 
> 次设备号（Minor Number）
> 
> • 作用：用来区分该类型设备中的具体哪一个实例（个体）。
> 
> • 理解：驱动程序可能控制多个同类硬件。比如有 3 个串口设备，它们的主设备号相同（都是串口驱动），但次设备号不同（比如 0、1、2），驱动程序通过次设备号来区分具体在操作哪一个串口。

该函数的主要作用可以概括为创建一个char_device_struct 对象 cd，从全局 `char_device_struct chrdevs[]`中获取一个可用的主设备号，随后将cd添加到全局数组中并将其设备号输出。


```c
struct cdev {
	struct kobject kobj;
	struct module *owner;
	const struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
} ;

#define CHRDEV_MAJOR_HASH_SIZE 255
static struct char_device_struct {
	struct char_device_struct *next;
	unsigned int major;
	unsigned int baseminor;
	int minorct;
	char name[64];
	struct cdev *cdev;		/* will die */
} *chrdevs[CHRDEV_MAJOR_HASH_SIZE];
```

### 创建cdev对象并关联设备号
接写了是在我们模块的__init代码中创建了调用了`cdev_init`函数，该函数作用就是初始化一个cdev的对象，并将操作表指针指向该对象的ops属性。
```c
/**
 * cdev_init() - initialize a cdev structure
 * @cdev: the structure to initialize
 * @fops: the file_operations for this device
 *
 * Initializes @cdev, remembering @fops, making it ready to add to the
 * system with cdev_add().
 */
void cdev_init(struct cdev *cdev, const struct file_operations *fops)
```

### 创建设备节点
下面，就需要把我们刚创建的cdev对象与在设备中申请的到的设备号关联起来了，通过`cdev_add`函数关联:
```c
/**
 * cdev_add() - add a char device to the system
 * @p: the cdev structure for the device
 * @dev: the first device number for which this device is responsible
 * @count: the number of consecutive minor numbers corresponding to this
 *         device
 *
 * cdev_add() adds the device represented by @p to the system, making it
 * live immediately.  A negative error code is returned on failure.
 */
int cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	int error;

	p->dev = dev;
	p->count = count;

	if (WARN_ON(dev == WHITEOUT_DEV))
		return -EBUSY;

	error = kobj_map(cdev_map, dev, count, NULL,
			 exact_match, exact_lock, p);
	if (error)
		return error;

	kobject_get(p->kobj.parent);

	return 0;
}
```

该函数首先初始化cdev对象的设备号以及设备数量，随后会将cdev对象登记到全局的cdev_map中(`kobj_map`函数)。由于cdev_map是根据设备号组织的，所以后续也可以很快的通过设备号找到cdev对象。
随后创建一个设备类的对象，再通过sysfs上创建设备节点即可完成内核模块设备节点的创建。

上述过程可以简单的总结一下:
1. 申请设备号
2. 创建cdev对象，并将对象与ops操作表关联
3. 将cdev对象与设备号关联

### 创建设备节点
创建`/dev`下设备节点通常需要以下两个步骤：
```c
my_class = class_create(THIS_MODULE, MYDEV_NAME);//创建设备类对象

//根据指定的设备号创建设备节点
my_device = device_create(my_class,NULL,char_dev,NULL,DEVICE_NAME);
```
创建设备节点的函数描述如下
```c
/**
 * device_create - creates a device and registers it with sysfs
 * @class: pointer to the struct class that this device should be registered to
 * @parent: pointer to the parent struct device of this new device, if any
 * @devt: the dev_t for the char device to be added
 * @drvdata: the data to be added to the device for callbacks
 * @fmt: string for the device's name
 *
 * This function can be used by char device classes.  A struct device
 * will be created in sysfs, registered to the specified class.
 *
 * Returns &struct device pointer on success, or ERR_PTR() on error.
 */
struct device *device_create(struct class *class, struct device *parent,
			     dev_t devt, void *drvdata, const char *fmt, ...)
```
我们注意到，这个函数参数是可变长度的，这因为该函数方便了申请了多个minor设备号的情况下设备的创建：
例如我们申请了四个minor设备号，就可以通过如下方式创建各自的设备节点
```c
alloc_chrdev_region(&char_dev,0,4,DEVICE_NAME);
cdev_init(&char_cdev,&char_dev_ops);
cdev_add(&char_cdev,char_dev,4);

//分别为每个minor调用device_create
for (int i = 0; i < 4; i++) {
    dev_t devt = MKDEV(MAJOR(dev_base), MINOR(dev_base) + i);

    my_devices[i] = device_create(my_class, NULL, char_dev, NULL,
                                  "mydev%d", i);//还是调用了四次创建设备节点，但直接用printf的形式修改设备名称
    if (IS_ERR(my_devices[i])) {
        /* 回滚已经创建的设备 */
    }
}

/*驱动中通过inode判断用户态程序通过哪个设备节点调用了*/
static int open(struct inode *inode, struct file *file)
{
    unsigned int minor = iminor(inode);
    return 0;
}
```

### 通过文件系统接口调用设备
之前说到了内核模块就像是在内核中注册了一组系统调用，只是Linux上的这类"系统调用"通过文件系统接口调用，而我们天脉上的通过系统调用号。接下来，就是简单分析，追究一下Linux上用户态程序“发起系统调用”的过程。

#### 从用户态到VFS
用户态程序通过调用glibc的open函数，通过glibc发起系统调用，最终都会进入Linux中的`do_filp_open`函数
```c
    int fd = open(DEVICE_PATH,O_WRONLY);
    if(fd < 0){
        perror("open for write failed.\n");
        exit(EXIT_FAILURE);
    }

	/*Linux内部文件打开，三个参数依次是路径查找的起始目录描述符、文件路径名称、文件的openflag*/
	do_filp_open(dfd, tmp, &op);

```
由`do_filp_open`函数查找到文件路径以及对应设备节点inode，并且在初始化设备节点inode时，会设置如下属性：
```c
inode->i_rdev  = MKDEV(major, minor); /* 设备节点中记录的设备号 */
inode->i_fop   = &def_chr_fops;       /* 通用字符设备入口 */
```
然后调用设备节点打开函数：`vfs_open(path, f)`，再进入`do_dentry_open`。
`do_dentry_open`的核心流程为
```c
file->f_op = fops_get(inode->i_fop);
file->f_op->open(inode, file);
```
上面我们说到inode的f_op已经被初始化为内核中通用字符设备的`def_chr_fops`，那么该open函数实际调用为`chrdev_open(inode, file)`。该函数中核心的代码如下：
```c
static int chrdev_open(struct inode *inode, struct file *filp)
{
    const struct file_operations *fops;
    struct cdev *p;
    struct kobject *kobj;
    int idx;

    p = inode->i_cdev;

    if (!p) {
        kobj = kobj_lookup(cdev_map, inode->i_rdev, &idx);
        if (!kobj)
            return -ENXIO;

        p = container_of(kobj, struct cdev, kobj);

        inode->i_cdev = p;
        inode->i_cindex = idx;
        list_add(&inode->i_devices, &p->list);
    }

    fops = fops_get(p->ops);
    replace_fops(filp, fops);

    if (filp->f_op->open)
        return filp->f_op->open(inode, filp);

    return 0;
}
```
关键流程为：
1. 在cdev_map中根据设备号查找指定的cdev对象；
2. 获取cdev对象中的ops；
3. 替换当前的`def_chr_fops`为cdev的ops；
4. 调用ops->open().
此时，就通过对sysfs上的文件操作的方式调用到了对应内核模块的open接口。由于ops已经被替换到file对象中，并且返回了fd，后续就可以通过fd直接调用inode中的read、write接口等。



