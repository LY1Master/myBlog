# Linux hello module lab

这是一个最小的 Linux 内核模块实验，用来验证以下调用链：

```text
insmod -> finit_module(2) -> 加载 .ko -> 调用 module_init 指定的函数
rmmod  -> delete_module(2) -> 调用 module_exit 指定的函数
```

模块初始化和清理函数使用 `pr_info()` 输出。输出进入内核日志缓冲区，而不是终端
标准输出；加载或卸载后使用 `dmesg` 查看。

## 前置检查

在 Ubuntu Server 中执行：

```bash
uname -r
sudo apt update
sudo apt install -y build-essential linux-headers-"$(uname -r)"
test -d /lib/modules/"$(uname -r)"/build && echo "headers/build tree: OK"
```

构建树必须对应当前正在运行的内核版本。不要只安装 `linux-headers-generic`
后就假定它一定匹配，应通过上面的 `test` 确认。

## 构建

```bash
make
file hello.ko
modinfo hello.ko
```

`file hello.ko` 应显示 ELF relocatable 文件，`modinfo` 应显示许可证、描述和版本。

## 加载与卸载

```bash
sudo insmod ./hello.ko
lsmod | grep '^hello'
sudo dmesg | tail -n 10
sudo rmmod hello
sudo dmesg | tail -n 10
```

预期最后两行日志依次出现：

```text
hello_module: loaded
hello_module: unloaded
```

如果当前运行在容器中，`insmod` 通常会因权限或宿主内核限制失败。实验应放在完整的
Ubuntu Server 虚拟机或宿主机中完成。

## 清理构建产物

```bash
make clean
```

## 自测问题

1. `insmod` 和 `modprobe` 的区别是什么？
2. 为什么模块不能直接链接用户态的 glibc？
3. `__init` 和 `__exit` 分别表示什么？
4. 为什么 `pr_info()` 的输出要到 `dmesg` 里看？
5. `uname -r`、Kbuild 构建树和正在运行的内核为什么要匹配？
