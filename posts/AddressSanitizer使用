## AddressSanitizer
AddressSanitizer（简称为ASan）是Google开发的一个C/C++内存错误检测工具，通过在编译时插入探测代码来检测释放后使用、缓冲区溢出（下标越界）、内存泄露等内存错误。
## 使用方法
GCC编译器（4.8版本以上）自带了AddressSanitizer。
要使用AddressSanitizer，需要添加编译选项-fsanitize=address，GCC命令如下
```
 g++ -std=c++17 -g -O1 \
    -fsanitize=address \
    -fno-omit-frame-pointer \
    -Iinclude \
    src/Ring.cpp src/main.cpp \
    -o ring_demo_asan
```
-g ：生成调试信息，ASan 才能显示源码行号
-fsanitize=address : 开启 AddressSanitizer，并链接 ASan 运行时
-fno-omit-frame-pointer : 保留栈帧指针，让调用栈更完整

CMake使用
```
cmake -S . -B build-asan -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```
### 分析问题
使用ASan编译运行后，报错日志如下
```
==14188==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x502000000015 at pc 0x71f51eca1a6a bp 0x7ffcdc519330 sp 0x7ffcdc518aa8
READ of size 6 at 0x502000000015 thread T0
    #0 0x71f51eca1a69 in printf_common ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors_format.inc:563
    #1 0x71f51ecce33b in vprintf ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors.inc:1644
    #2 0x71f51ecd0180 in printf ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors.inc:1702
    #3 0x5ea2bcee0cff in Ring::show() /home/yan51/work/w3/ring/src/Ring.cpp:58
    #4 0x5ea2bcee046a in main /home/yan51/work/w3/ring/src/main.cpp:14
    #5 0x71f51e42a1c9 in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58
    #6 0x71f51e42a28a in __libc_start_main_impl ../csu/libc-start.c:360
    #7 0x5ea2bcee0284 in _start (/home/yan51/work/w3/ring/build-asan/ring_demo+0x1284) (BuildId: a85f3d4bfdeb89aaf11e92660cb3a1aee2fbc186)

0x502000000015 is located 0 bytes after 5-byte region [0x502000000010,0x502000000015)
allocated by thread T0 here:
    #0 0x71f51ecfd9c7 in malloc ../../../../src/libsanitizer/asan/asan_malloc_linux.cpp:69
    #1 0x5ea2bcee06bb in Ring::Ring(int) /home/yan51/work/w3/ring/src/Ring.cpp:9
    #2 0x5ea2bcee042b in main /home/yan51/work/w3/ring/src/main.cpp:10
    #3 0x71f51e42a1c9 in __libc_start_call_main ../sysdeps/nptl/libc_start_call_main.h:58
    #4 0x71f51e42a28a in __libc_start_main_impl ../csu/libc-start.c:360
    #5 0x5ea2bcee0284 in _start (/home/yan51/work/w3/ring/build-asan/ring_demo+0x1284) (BuildId: a85f3d4bfdeb89aaf11e92660cb3a1aee2fbc186)

SUMMARY: AddressSanitizer: heap-buffer-overflow ../../../../src/libsanitizer/sanitizer_common/sanitizer_common_interceptors_format.inc:563 in printf_common
Shadow bytes around the buggy address:
  0x501ffffffd80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x501ffffffe00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x501ffffffe80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x501fffffff00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x501fffffff80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x502000000000: fa fa[05]fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x502000000080: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x502000000100: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x502000000180: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x502000000200: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
  0x502000000280: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==14188==ABORTING
```
1. 分析错误类型 heap-buffer-overflow：堆缓冲区溢出，常见与数组动态下标越界
2. 关键信息： “READ of size 6 at 0x502000000015 thread T0”  线程T0尝试从地址0x502000000015读取第6个字节
3. 调用栈信息：定位错误发生在 Ring::show() /home/yan51/work/w3/ring/src/Ring.cpp:58
4. 访问的越界地址0x502000000015是在地址region [0x502000000010,0x502000000015)之后0个字节，并且[0x502000000010,0x502000000015)地址是被 Ring::Ring(int) /home/yan51/work/w3/ring/src/Ring.cpp:9 分配的
5. 查找对应代码，发现第9行 buffer = (char*)malloc(5); 申请了5个字节空间，58行 printf("ring is %s\n",buffer); 由于buffer中没有'\0'导致读取了第6个字节。
## ASan 能检查出的错误类型
1. 释放后使用(use after free)：访问已释放的动态分配内存
2. 返回后使用(use after return)：在函数返回后访问局部变量的地址。
3. 作用域外使用(use after scope)：在作用域结束后访问局部变量的地址。
4. 堆缓冲区溢出(heap buffer overflow)：动态分配数组的下标越界访问。
5. 栈缓冲区溢出(stack buffer overflow)：栈数组的下标越界访问。
6. 全局缓冲区溢出(global buffer overflow)：全局数组的下标越界访问。
7. 内存泄露(memory leaks)：动态分配的内存没有被释放。
8. 静态初始化顺序问题(static initialization order fiasco)：在同一个翻译单元中的全局变量按它们出现的顺序初始化，但不同翻译单元中的全局变量的初始化顺序是不确定的。例如：此时b2的值可能随着编译文件顺序变为2或5
```c++
int a = 1;
int b1 = a + 2;  // b1 becomes 3
```
```c++
extern int b1;
int b2 = b1 + 2;  // b2 becomes 2 or 5

int main() {
    std::cout << b1 << ' ' << b2 << '\n';
    return 0;
}
```
```
$ g++ -std=c++17 -o test test.cc foo.cc && ./test 
3 2
$ g++ -std=c++17 -o test foo.cc test.cc && ./test 
3 5
```