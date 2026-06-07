# OS Stage 0 学习说明

OS Stage 0 的目标是建立现代 x86-64 OS 必须依赖的硬件边界，而不是马上写完整进程和调度器。

```text
Machine       模拟机器入口
BootContext   kernel 启动资源视图
Kernel        启动状态机
Address/Page  物理/虚拟地址和 page 工具
ThreadContext CPU 状态 + kernel stack + 线程状态
```

## 为什么不直接做进程？

进程和线程最终都要落到硬件状态：

```text
当前 RIP 是多少？
RAX/RBX/.../RSP 是什么？
RFLAGS 是什么？
RSP 指向哪个 kernel stack？
一次内存访问落到哪个物理地址？
异常/中断/系统调用如何进入 kernel？
```

如果没有 `CpuState`，scheduler 没有东西可保存。如果没有 `ThreadContext`，线程只是一个 ID。如果没有 `Machine` 和 `BootContext`，kernel 不知道自己能访问哪些硬件资源。

## Machine 和 BootContext

`Machine` 是硬件 facade：

```text
Machine = PhysicalMemory + MemoryBus
```

`BootContext` 是 kernel 启动时的资源视图：

```text
Machine&
PhysicalMemory&
MemoryBus&
physical_memory_size_bytes
physical_page_count
bootstrap_processor_count
```

它不拥有 `Machine`，这让测试、emulator、多核拓扑和设备模型可以复用同一个硬件对象。

## ThreadContext

`ThreadContext` 表示一个内核线程的 CPU 保存区：

```text
ThreadId
ThreadState
CpuState
kernel stack bottom/top
```

构造成功后保证：

```text
ThreadId 合法
kernel stack bottom 页对齐
kernel stack size 是非零页倍数
RSP 初始化为 kernel_stack_top
```

x86-64 栈通常向低地址增长，所以初始 `RSP` 放在 stack top。

## 和 x86-64 CPU Stage 0/1 的关系

当前 `CpuState` 包含：

```text
RegisterBank
Rflags
RIP
halted
```

`RFLAGS` 当前建模了 `CF/ZF/SF/OF`。`ADD/SUB/CMP` 会更新这些状态位，`JE/JNE` 读取 `ZF`。这比 RV 的显式寄存器比较更复杂，但更贴近 x86-64 现实。

CPU Stage 1 已经加入 `ExecutableImage -> Decoder -> Instruction` 的真实 byte image 路径，`Program` 对象路径继续用于教学和语义测试。`HLT` 当前仍作为停止点。后续系统调用和中断阶段会引入：

```text
SYSCALL/SYSRET
IDT/GDT/TSS
timer interrupt
page fault
trapframe
```

## 下一步

合理顺序：

```text
1. CALL/RET/PUSH/POP 和基础栈语义
2. LEA、TEST、AND/OR/XOR、SETcc/CMOVcc
3. exception/syscall/interrupt
4. paging/MMU/TLB
5. trapframe + context switch
6. syscall ABI + scheduler
7. 进程地址空间和用户/内核切换
8. 原子操作、多核、cache/TLB 交互
```

这样学习者能从真实 x86-64 的 CPU 状态走到现代 OS，而不是只看抽象 API。
