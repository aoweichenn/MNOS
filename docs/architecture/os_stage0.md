# OS Stage 0 学习说明

这一阶段不是“直接开始写线程和进程”，而是先建立 OS 和 CPU 之间最关键的边界：

```text
硬件资源        Machine
启动输入        BootContext
内核状态        Kernel
地址模型        PhysicalAddress / VirtualAddress
分页基础        4KiB page 工具
线程执行上下文  ThreadContext = CpuState + kernel stack + ThreadState
```

## 1. 为什么现在不直接做进程和调度器？

线程、进程、多核听起来是 OS 的重点，但真正落到硬件上时，它们必须依赖几个底层事实：

```text
1. CPU 当前寄存器是什么？
2. 当前 RIP 指向哪条指令？
3. 当前 RFLAGS 是什么？
4. 当前栈指针 RSP 指向哪里？
5. 这段内存访问最终落到哪个物理地址？
6. 这个上下文是 READY、RUNNING、BLOCKED 还是 DEAD？
```

如果没有 `CpuState`，调度器就没有东西可保存。如果没有 `ThreadContext`，线程只是一个名字。如果没有 `BootContext` 和 `Machine`，kernel 不知道自己能使用多少物理内存、通过哪条 bus 访问硬件。

所以 Stage 0 的目标是把后续线程/进程所需的“硬件支点”建出来。

## 2. Machine 是什么？

`platform::Machine` 是一个 facade，也就是把多个底层对象组合成一个更高层入口：

```cpp
class Machine final
{
public:
    explicit Machine(std::size_t physical_memory_size_bytes);

    cpu::PhysicalMemory& physical_memory() noexcept;
    cpu::MemoryBus& memory_bus() noexcept;

private:
    cpu::PhysicalMemory physical_memory_;
    cpu::MemoryBus memory_bus_;
};
```

这里成员顺序很重要：

```text
physical_memory_ 必须先构造
memory_bus_      才能保存指向 physical_memory_ 的引用/指针
```

这对应真实机器里的关系：

```text
CPU -> memory bus -> physical memory
```

以后加入 cache 或 MMU 时，这个链路可以扩展成：

```text
CPU -> bus -> cache -> MMU/page table -> physical memory
```

CPU executor 仍然只需要知道 bus，不需要知道 OS 的全部内部细节。

## 3. BootContext 是什么？

`BootContext` 表示 kernel 启动时拿到的硬件资源视图：

```cpp
platform::Machine machine(64 * 4096);
kernel::BootContext context{machine, 4};
```

它当前保存：

```text
Machine&                 当前模拟硬件
bootstrap_processor_count 启动时看到的处理器数量
physical_memory_size      物理内存大小
physical_page_count       完整物理页数量
```

它不拥有 `Machine`。这是一种明确的生命周期设计：

```text
Machine 生命周期更长
BootContext 只是启动阶段的视图
Kernel 通过 BootContext 读硬件信息
```

如果 `BootContext` 自己拥有所有硬件资源，后续测试、emulator、多核拓扑就很难灵活组合。

## 4. Kernel 现在做什么？

当前 `Kernel` 是一个极小的启动状态机：

```cpp
kernel::Kernel os_kernel{context};
os_kernel.boot();
```

它只保证两个不变量：

```text
1. kernel 不能重复 boot
2. 至少有一个完整 4KiB 物理页才允许 boot
```

这看起来很小，但它的意义是把“启动状态”从散落的 bool 和裸函数里抽出来。后面可以继续加入：

```text
panic handler
boot allocator
GDT/IDT 模型
interrupt controller
timer
init thread
```

而不需要推翻 `Kernel::boot()` 这个入口。

## 5. 为什么要区分 PhysicalAddress 和 VirtualAddress？

真实 OS 里，物理地址和虚拟地址不是一回事：

```text
PhysicalAddress: 真实物理内存上的地址
VirtualAddress : 某个地址空间中 CPU 指令看到的地址
```

在没有 MMU 前，二者可能数值相等。但一旦进入分页：

```text
VirtualAddress 0x400000
    -> page table
PhysicalAddress 0x12345000
```

如果代码里全部用 `std::uint64_t`，初学者很容易把物理地址和虚拟地址混用，而且编译器无法帮你发现。

现在使用强类型：

```cpp
mm::PhysicalAddress pa{0x1000};
mm::VirtualAddress va{0x1000};
```

这两个类型的底层都是 64 位整数，但类型不同。这样后续函数可以明确表达自己需要哪种地址：

```cpp
PageFrame frame_from_physical(mm::PhysicalAddress address);
PageTableEntry translate(mm::VirtualAddress address);
```

这属于现代 C++ 里常见的 strong typedef 思想：用类型系统阻止错误组合。

## 6. page 工具为什么是位运算？

x86-64 常见页大小是 4KiB：

```text
4KiB = 4096 = 2^12
```

所以一个地址可以拆成：

```text
高位: page number
低 12 位: page offset
```

例如：

```text
address = 0x2345
page    = 0x2
offset  = 0x345
```

代码中：

```cpp
page_number = address >> 12;
offset      = address & 0xFFF;
align_down  = address & ~0xFFF;
```

这不是炫技，而是硬件分页机制本来就是这样工作的。页大小是 2 的幂时，除法和取模可以变成位移和掩码。

Stage 0 里 page 工具已经提供：

```text
is_page_aligned
align_down
align_up
page_number
offset_in_page
page_count_for_bytes
```

`align_up` 和 `page_count_for_bytes` 还做了 overflow 检查，因为地址接近 `uint64_t` 最大值时，简单的 `value + 4095` 可能溢出。

## 7. ThreadContext 为什么现在就有 kernel stack？

真实内核线程不是只有一个 ID。它至少要能保存：

```text
CpuState    寄存器、RIP、RFLAGS、halted
ThreadId    线程身份
ThreadState READY/RUNNING/BLOCKED/DEAD
kernel stack 栈范围
```

当前 `ThreadContext` 构造时会把 CPU 的 `RSP` 初始化到 kernel stack top：

```text
stack bottom 低地址
stack top    高地址，一般作为初始 RSP
```

x86-64 栈通常向低地址增长，所以初始 RSP 放在 top，而不是 bottom。

示例：

```cpp
sched::ThreadContext thread{
    sched::ThreadId::first_kernel_thread(),
    mm::VirtualAddress{0x8000}
};

auto rsp = thread.cpu_state().registers().read(cpu::RegisterId::RSP);
```

`reset_cpu_state()` 会清空寄存器和 flags，但保留这个线程的 kernel stack 约束，并重新设置 RSP。这样以后做 context switch 时，每个线程都有自己的 CPU 保存区和栈。

## 8. 这一阶段引入了哪些现代 C++ 思想？

强类型值对象：

```text
PhysicalAddress / VirtualAddress / ThreadId
```

这些类型很小，按值传递，没有堆分配，但能显著减少裸整数混用。

Facade：

```text
Machine = PhysicalMemory + MemoryBus
```

外部只需要通过 `Machine` 访问硬件组合，不需要知道成员构造细节。

表驱动枚举：

```text
ThreadState -> "READY" / "RUNNING" / ...
```

沿用 CPU 里的 `EnumMap` 思路，减少重复 switch。

RAII 和对象不变量：

```text
ThreadContext 构造成功后，一定有合法 ThreadId、页对齐 stack、合法 RSP。
BootContext 构造成功后，一定有非空 memory、至少一个 processor。
```

异常只出现在构造/状态设置这类边界；正常查询函数尽量是 `noexcept`。

## 9. 下一步应该做什么？

现在还不应该直接做“完整进程”。更合理的下一阶段是 OS Stage 1：

```text
1. PhysicalPageAllocator
2. KernelHeap 或 BootAllocator
3. PageFrame 强类型
4. 简单地址空间模型
5. 基于 ThreadContext 的最小 run queue
```

原因是线程/进程调度一定会用到内存分配：

```text
创建线程 -> 分配 kernel stack
创建进程 -> 分配地址空间/page table
阻塞/唤醒 -> 操作调度队列
```

没有页分配器就强行做线程，只能写很多临时栈地址和裸数组，后面会返工。
