## 前言
2025年在所里搞了天脉轻量操作系统内核，一直想总结一下当时的工作，奈何保密要求，没有办法在详细记录，梳理其细节。因此借用FreeRTOS内核分析一下主流轻量化嵌入式操作系统内核的相关功能。其实轻量化之所以叫轻量，一方面是由于其本身体量(映像大小与占用运行空间大小)很小，可以在资源受限设备上运行，另一方面因为其内核代码量及其精简：只包括必须的任务、调度、内存管理、tick处理。各个轻量RTOS(FreeRTOS、uCOS、RT-Thread、LiteOS、以及天脉轻量)都差不多，只是实现上的大同小异了。因此借用FreeRTOS来记录、分析也未尝不可。我大概会写四篇文章，包括轻量RTOS的链接启动、任务调度与tickless、内存管理与slab，这样大概就能覆盖全轻量化操作系统内核了。最后一篇会关于分区操作系统，毕竟这几年的主要工作精力献给了它。


### FreeRTOS先搞来内核源码和设备模板
我们以最经典的FreeRTOS+STM32F103为例：
先clone来内核源码：[FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel.git)

再去下载STM32F103的标准项目模板：[STM32F103 标准外设库 (SPL) 项目模板](https://github.com/qianye60/STM32F103-SPL-Template)


### FreeRTOS先从链接脚本与上电第一条指令开始
#### 链接脚本
说到RTOS，就不得不提链接脚本，因为有千万种设备，每种设备的存储空间都有不同，那么如何生成可执行文件能够适配特定硬件的内存布局呢，这就需要链接脚本文件。链接脚本文件的作用就是告诉链接器，目标文件中的各段放到哪个物理地址。

首先看我们F103链接脚本的第一句：`ENTRY(Reset_Handler)`,代表我们使用 Reset_Handler 作为程序入口地址。
ld有多种指定程序入口方式：(优先级逐渐降低)
- ld命令 -e选项；
- 连接脚本中ENTRY(symbol)的命令；
- 如果定义了_start符号，使用_start符号的值；
- 如果存在.text段，则使用.text段的第一字节的地址；
- 使用地址0x00000000。

既然看了链接脚本，不妨多看一点：

```c
/* Memory definition for STM32F103C8T6 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Stack size */
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;
```

F103上，Flash空间从0x08000000开始，大小64K，RAM空间从0x20000000开始，大小20K

_estack 表示栈顶地址，从0x20005000开始，向下(低地址)生长。简单解释一下栈的PUSH：ARM的push一般等价于
```
SUB SP, SP, #4
STR R0, [SP]
```
也就是说F103一共只有0x5000字节（20KiB）的RAM，链接脚本还额外预留了至少0x200字节的传统heap和0x400字节的MSP主栈。这里的0x200和0x400只是预留下限，不是整块RAM，真正占用还要包括.data、.bss、FreeRTOS的heap数组和任务栈等等，确实是很受限呢。

再看看代码段的分配，代码段竟然直接安排在flash上原地执行，不进行重定位。这也是因为资源太过受限，如果代码段要搬家到RAM种执行，空间会非常紧张。

> Flash 是否一定比 RAM 慢
通常 RAM 访问更快，但不能简单认为“Flash 代码一定慢得不能用”。
>
> STM32F103 在 72 MHz 下，Flash 通常需要 2 个等待周期，而 SRAM 是零等待周期。不过 F103 带有 Flash 预取缓冲器：
> - 顺序执行时，预取能连续取指令，性能可以比较接近零等待；
> - 频繁跳转、查表或紧循环时，等待周期更明显；
> - RAM 执行通常更快，尤其是在这些场景下。
> 因此，Flash 上的代码并非“慢两倍”，但确实可能比 RAM 慢。

后面的.data段总算重定位到RAM中，而.bss段由于全是0，直接放到RAM中，后续清零就好了。

把链接结果串起来，F103当前的镜像布局大概是：

```text
Flash
0x08000000  .isr_vector        中断向量表
            .text              程序代码
            .rodata            只读常量
            .data初始值副本    .data的LMA

RAM
0x20000000  .data              运行时变量，VMA
            .bss               未初始化数据，不占Flash
            链接器预留的heap/stack空间
            ...
0x20005000  _estack            初始MSP，栈从这里向下生长
```

这里最关键的是`.data`有两个地址：Flash里的LMA负责保存初始值，RAM里的VMA才是程序运行时访问的地址。Reset_Handler要做的第一件事情，就是把这些初始值从Flash搬到RAM。

#### 第一条指令 Reset_Handler
这时候可以开始我们的第一条指令了：
```c
Reset_Handler:
  ldr   sp, =_estack    /* set stack pointer */

/* Copy the data segment initializers from flash to SRAM */
  movs r1, #0
  b LoopCopyDataInit
```
这里要先澄清一个很容易混淆的点：链接脚本里的`ENTRY(Reset_Handler)`只是把Reset_Handler写进ELF的入口字段，并不代表它位于Flash的第一条指令。ARMv7-M复位时，硬件固定从地址0加载初始MSP，从地址4加载Reset PC。STM32F103从Flash启动时，会先把Flash映射到地址0，所以CPU通过地址0读到的仍然是0x08000000处的向量表。进入Reset_Handler以后，代码先构建栈指针，然后初始化R1寄存器，再直接进行数据段搬家、BSS段清零，随后跳转至SystemInit函数，初始化时钟什么的都是根据硬件要求来，这里有一个重点，初始化VTOR寄存器！
> VTOR（Vector Table Offset Register）：用于设置异常向量表的基地址。
SystemInit里是这样写的：
```c
  SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET; /* Vector Table Relocation in Internal FLASH. */
```
SystemInit把VTOR设置为0x08000000，也就是Flash的起始地址。复位时硬件还不会使用VTOR，而是通过地址0的Flash映射读取向量表；等VTOR配置好以后，后续异常就直接根据VTOR到Flash里查表。上文说到，.text和中断向量表都在flash上，0x08000000是Flash的起始地址，那我们看看那里有什么。链接脚本中看到：.isr_vector在该地址，而该段在startupxxx.S中定义，包括了很多函数入口：
```c
.section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
  .size g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  ...
  ...
```
> ARMv7-M 架构规定，每个异常有一个异常编号 N，异常向量是 32 位地址，位于：
> VectorAddress(N) = VTOR + 4 × N
> 复位后的标准系统异常编号和位置是：
>
> 异常编号	偏移	内容
>
> 0     0x00        初始 MSP，不是异常入口
>
> 1	0x04	Reset
>
> 2	0x08	NMI
>
> 3	0x0C	HardFault
>
> 4	0x10	MemManage
>
> 5	0x14	BusFault
>
> 6	0x18	UsageFault
>
> 7-10	0x1C-0x28	保留
>
> 11	0x2C	SVCall
>
> 12	0x30	DebugMonitor
>
> 13	0x34	保留
>
> 14	0x38	PendSV
>
> 15	0x3C	SysTick
>
> 16	0x40	外部中断 IRQ0
>
> 17	0x44	外部中断 IRQ1
>

所以SystemInit函数做了一个很重要的事情：将中断向量表与VTOR做出关联。知道了这一点，那么如果我们想自定义中断处理程序，就可以直接替换g_pfnVectors代码段中指定的符号了，但是不推荐这么做。FreeRTOS推荐的做法是启动文件不动，在 C 文件里实现一个与向量表中同名的强符号函数。
启动文件中将中断处理程序都声明为了弱引用，
```c
.weak TIM2_IRQHandler
.thumb_set TIM2_IRQHandler, Default_Handler
```
因此，普通外设中断只需要声明同名函数，再设置中断优先级，使能中断即可。
```c
void TIM2_IRQHandler(void)
{
    /* 中断处理 */
}
NVIC_SetPriority(TIM2_IRQn, 5);
NVIC_EnableIRQ(TIM2_IRQn);
```

不过SVC、PendSV、SysTick这三个异常不能随便替换，它们已经交给FreeRTOS接管了。FreeRTOSConfig.h里一般会这样定义：

```c
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
```

这样port.c里的移植函数就会以CMSIS标准异常名参与链接，覆盖启动文件里的弱默认处理函数。普通外设中断可以自己定义强符号，但这三个核心异常不能随意替换，否则可能无法启动第一个任务，或者根本没有任务切换。如果想在Tick里增加自己的业务，可以打开`configUSE_TICK_HOOK`并使用`vApplicationTickHook()`，不要直接覆盖`SysTick_Handler`。

##### 额外说一些，FreeRTOS的中断优先级

回到前面的例子：

```c
NVIC_SetPriority(TIM2_IRQn, 5);
```

这里为什么偏偏是5？原因要从Cortex-M3的`BASEPRI`寄存器和FreeRTOS的临界区说起。

F103一共有4个有效的NVIC优先级位，所以优先级范围是0~15，并且数值越小，硬件优先级越高：

| NVIC优先级 | 含义 |
| --- | --- |
| 0 | 最高优先级 |
| 1~4 | 高于FreeRTOS系统调用阈值 |
| 5 | FreeRTOS系统调用阈值 |
| 6~14 | 低于阈值 |
| 15 | 最低优先级 |

FreeRTOS的配置一般是这样：

```c
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
```

带`LIBRARY`的两个宏是程序员看的逻辑优先级，后面两个宏才是真正写入硬件的8位NVIC优先级字段。因为F103只实现高4位，所以：

```text
configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 << 4 = 0x50
```

FreeRTOS进入临界区时，会把`BASEPRI`设置成`0x50`：

```asm
MSR BASEPRI, #0x50
```

这个时候并不是把中断全部关掉，而是屏蔽**数值大于等于0x50**的中断。换句话说：

```text
优先级0~4  ：不屏蔽，仍然可以抢占FreeRTOS临界区
优先级5~15 ：屏蔽，不能抢占FreeRTOS临界区
```

所以“调用FromISR接口的中断优先级必须大于等于5”，本质上是要求这个中断在内核临界区执行期间不能抢占进来。

为什么不能抢占？因为队列、信号量、事件组这些对象并不是一个简单的变量，它们内部维护着任务等待链表。`xQueueSendFromISR()`、`xSemaphoreGiveFromISR()`这类接口会修改等待任务、就绪链表，甚至更新`pxCurrentTCB`。如果内核刚改到一半，一个优先级0~4的中断突然抢进来，又调用了一次FromISR接口，那么同一个链表或任务状态就被重入了，最后很可能出现任务丢失、链表损坏，甚至随机HardFault。这种问题通常还很难复现，所以FreeRTOS干脆规定了系统调用所能接受的最高中断优先级。

这里还有一个容易绕晕的点：优先级等于5也可以调用FromISR接口。因为临界区把`BASEPRI`也设置成了`0x50`，优先级5对应硬件值同样是`0x50`，它在临界区内也会被屏蔽，因此不会在内核修改关键数据结构时抢占进来。所以允许的范围是5~15，而不是必须大于5。

举个带FreeRTOS的ISR就清楚了：

```c
void TIM2_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if ((TIM2->SR & TIM_SR_UIF) != 0U)
    {
        TIM2->SR &= ~TIM_SR_UIF;

        xQueueSendFromISR(
            xEventQueue,
            &event,
            &xHigherPriorityTaskWoken);

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
```

ISR里不能调用`xQueueSend()`、`vTaskDelay()`这类普通任务接口，只能调用带`FromISR`后缀的版本，最后再用`portYIELD_FROM_ISR()`决定是否在退出中断时切换任务。

那优先级0~4的中断是不是完全不能用？当然不是。它们通常留给电机保护、电源故障、紧急停机这种对响应时间要求极高的硬件事件。这类中断可以打断FreeRTOS临界区，所以不能调用FreeRTOS API。它们一般只做最少的硬件处理，然后设置一个`volatile`标志，把后续复杂逻辑交给普通任务：

```c
void EmergencyStop_IRQHandler(void)
{
    /* 关PWM、记录硬件状态 */
    gEmergencyStopFlag = 1U;
}
```

还有一点，PendSV和SysTick在Cortex-M3 port里都被设置成最低优先级15。这样FreeRTOS正在临界区内修改链表时，即使SysTick到期或者PendSV已经挂起，也不会立刻切换任务，而是等退出临界区后再处理。上下文切换必须能延期，但不能被中途打断，这也是FreeRTOS依赖BASEPRI实现临界区的基础。

最后用CMSIS设置优先级时，直接写逻辑值就行：

```c
NVIC_SetPriority(TIM2_IRQn, 5);
NVIC_EnableIRQ(TIM2_IRQn);
```

不要把自己的`5`直接写进NVIC IP寄存器，CMSIS会根据芯片实际优先级位数自动左移成`0x50`。

把这一节压缩成一句话：

> NVIC优先级数值越小，中断越紧急。FreeRTOS在Cortex-M3上用`BASEPRI=0x50`屏蔽优先级5~15，保护内核临界区，因此只有优先级数值大于等于5的中断才能安全调用`FromISR`接口；优先级0~4可以用于极低延迟的硬件事件，但不能调用FreeRTOS API。

#### 用户代码main
FreeRTOS官方给的示例代码如下
```c
int main( void )
{
    static StaticTask_t exampleTaskTCB;
    static StackType_t exampleTaskStack[ configMINIMAL_STACK_SIZE ];
    ( void ) printf( "Example FreeRTOS Project\n" );
    ( void ) xTaskCreateStatic( &exampleTask,
                                "example",
                                configMINIMAL_STACK_SIZE,
                                NULL,
                                configMAX_PRIORITIES - 1U,
                                &( exampleTaskStack[ 0 ] ),
                                &( exampleTaskTCB ) );
    /* Start the scheduler. */
    vTaskStartScheduler();
    for( ; ; )
    {
        /* Should not reach here. */
    }
    return 0;
}
```
第一次看到这块的时候我很震惊，怎么就直接开始创建任务，使能调度了？！ 任务管理初始化在哪？tick初始化在哪？堆初始化在哪？！！！
随着看代码的深入才知道FreeRTOS采用 *首次使用时延迟初始化* 的形式，在首次创建任务时再初始化。

#### 从复位到第一个任务

把硬件启动和FreeRTOS接起来，整个握手过程可以串成一条线：

```text
上电/复位
  -> 从地址0加载初始MSP和Reset PC
  -> Reset_Handler
  -> 复制.data、清零.bss
  -> SystemInit设置时钟和VTOR
  -> main
  -> xTaskCreate创建任务
  -> vTaskStartScheduler
  -> xPortStartScheduler
  -> prvPortStartFirstTask
  -> SVC异常
  -> SVC_Handler恢复第一个任务的上下文
  -> 第一个任务开始运行
```

所以`main()`并不是FreeRTOS启动的终点，它只是把CPU交还给内核的入口。任务链表、空闲任务、定时器任务、SysTick和第一次上下文切换都藏在`vTaskStartScheduler()`之后，这也正是下一章要继续拆开的部分。

### 参考资料

- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/)
- [STM32F10xxx Cortex-M3 Programming Manual（PM0056）](https://www.st.com/resource/en/programming_manual/cd00164185.pdf)
- [STM32F103xx Reference Manual（RM0008）](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [FreeRTOS Cortex-M3/M4F Port](https://www.freertos.org/RTOS-Cortex-M3-M4.html)
