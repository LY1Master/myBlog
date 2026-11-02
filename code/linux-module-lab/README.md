# Linux 字符设备实验：char_echo

这个实验实现一个固定内核缓冲区支持的最小字符设备。写入会替换上一次内容，
读取会按每个 `open()` 独立的偏移分多次返回数据。

## 文件

| 文件 | 作用 |
|---|---|
| `module/char_echo.c` | 字符设备模块源码 |
| `module/Makefile` | 只调用 Kbuild 编译内核模块 |
| `test/char_echo_test.c` | 用户态读写测试 |
| `test/Makefile` | 只使用 C 编译器编译测试程序 |
| `run_test.sh` | 编译、加载、创建设备节点、测试、卸载 |

目录分层如下：

```text
linux-module-lab/
├── module/
│   ├── char_echo.c
│   └── Makefile
├── test/
│   ├── char_echo_test.c
│   └── Makefile
├── run_test.sh
└── README.md
```

## 前置条件

- 完整 Linux 虚拟机或允许加载模块的 Linux 主机，不能在普通容器中完成加载；
- `/lib/modules/$(uname -r)/build` 存在；
- `/proc/sys/kernel/modules_disabled` 为 `0`；
- 当前用户可执行 `sudo`。

先检查：

```bash
uname -r
test -d "/lib/modules/$(uname -r)/build" && echo "build tree: OK"
cat /proc/sys/kernel/modules_disabled
```

如果构建树不存在，安装与当前运行内核完全匹配的头文件：

```bash
sudo apt update
sudo apt install "linux-headers-$(uname -r)"
```

## 自动测试

```bash
chmod +x run_test.sh
./run_test.sh
```

成功时用户态最后一行应为：

```text
char_echo_test: PASS
```

内核日志中应依次出现：

```text
char_echo: loaded: major=<动态主设备号> minor=0
char_echo: open: ...
char_echo: release: ...
char_echo: unloaded
```

## 手动观察

```bash
make -C module
make -C test
sudo insmod ./module/char_echo.ko
grep char_echo /proc/devices
sudo mknod /dev/char_echo c <主设备号> 0
sudo chmod 0666 /dev/char_echo
./test/char_echo_test
printf 'kernel echo\n' > /dev/char_echo
cat /dev/char_echo
sudo rmmod char_echo
sudo rm -f /dev/char_echo
sudo dmesg | tail -n 20
```

清理两个构建目录：

```bash
make -C test clean
make -C module clean
```

## 一条读调用如何到达驱动

```text
用户态 read(fd, buf, count)
  -> read syscall
  -> VFS 根据 fd 找到 inode 和 file
  -> 设备 inode 找到注册过的 cdev
  -> 调用 cdev->ops->read，也就是 char_echo_fops.read
  -> copy_to_user 把内核缓冲区复制到用户地址空间
```

用户态 `read/write` 与驱动回调不是同名函数的直接链接关系。VFS 负责根据文件类型、
设备号以及 `file_operations` 分派到具体驱动。

## 关键概念

### 设备号与 mknod

`dev_t` 由主设备号和次设备号组成：

```text
主设备号：选择哪一类/哪个驱动
次设备号：选择该驱动管理的具体设备实例
```

本模块用 `alloc_chrdev_region()` 动态申请一个主设备号，因此不在源码里写死。
`mknod /dev/char_echo c <major> 0` 创建用户态可见的设备文件；其中 `c` 表示字符设备。

### 为什么不能直接解引用用户指针

内核态和用户态拥有不同的地址空间与访问规则。用户指针在当前进程上下文中虽然有虚拟
地址意义，但直接读取存在权限、缺页、并发修改和错误来源等问题，可能引发内核 oops。
应使用 `copy_from_user()` / `copy_to_user()` 完成带检查的数据复制。

两者返回“未能复制的字节数”，不是成功复制的字节数：

```text
0       -> 全部复制成功
非 0    -> 有多少字节没有复制成功，通常返回 -EFAULT
```

### 返回值与 offset

- `read()` 返回 0 表示当前已读完；
- `write()` 返回实际接受并保存的字节数；
- 本实验最多保存 255 字节，写入更长数据时只接受前 255 字节；
- `read_pos` 放在每个 `open()` 独立的私有状态中，因此两个同时打开的文件描述符互不推进对方的读位置；
- 本实验没有实现 `llseek`，使用 `no_llseek` 明确拒绝随机定位。

### 锁保护什么

全局 `char_echo_lock` 保护共享的内核缓冲区与长度。`open()` 创建的 `read_pos` 是
每个文件描述符私有的，不需要由该全局锁保护。

## 面试表述

> 我实现了一个最小 Linux 字符设备，通过动态主设备号和 `cdev` 把设备号映射到
> `file_operations`。用户态 `read/write` 经 VFS 分派到驱动回调，驱动使用
> `copy_to_user/copy_from_user` 在内核缓冲区和用户缓冲区之间复制，并用
> `mutex` 保护共享设备状态。模块初始化与清理路径按资源申请顺序反序释放。

这条表述只描述已经实现的功能，不能扩展成“生产级驱动”或“具备多年
Linux 驱动开发经验”。

## 自测

1. 用户态调用 `read()` 后，VFS 根据哪些信息找到本驱动的 `read` 回调？
2. 为什么 `copy_to_user()` 返回 `0` 才是成功？
3. 主设备号和次设备号分别用于什么？
4. 为什么 `char_echo_file.read_pos` 不放进全局静态变量？
5. 模块初始化中 `cdev_add()` 失败后，为什么还要调用
   `unregister_chrdev_region()`？
