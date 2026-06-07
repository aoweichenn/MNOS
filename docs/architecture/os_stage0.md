# OS Stage 0 学习说明

OS Stage 0 的目标是建立现代 x86-64 OS 必须依赖的硬件边界，而不是马上写完整进程和调度器。Stage 5 已在这个边界上补齐了第一版进程、地址空间、缺页处理、scheduler 和 syscall ABI；Stage 6 进一步补入 core topology、`LOCK` 原子指令和 x86 TSO 教学内存模型；Stage 7 加入 local APIC/IOAPIC、timer interrupt、抢占 tick、sleep/wait queue、IPI、PCID/INVLPG/TLB shootdown 和 scheduler handoff 入口；Stage 8 加入 cache、pipeline 和 perf counter 第一版性能硬件底座。

```text
Machine       模拟机器入口，持有物理内存、MemoryBus、core topology
BootContext   kernel 启动资源视图
Kernel        启动状态机 + Stage7 APIC/timer/sleep/shootdown 编排
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
Machine = PhysicalMemory + MemoryBus + CoreTopology
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

`BootContext` 不拥有 `Machine`，这让测试、emulator、多核拓扑和设备模型可以复用同一个硬件对象。Stage 6 后，`BootContext` 会校验启动处理器数量不能超过 `Machine` 的 core topology。

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

## 和 x86-64 CPU Stage 0/1/2/3 的关系

当前 `CpuState` 包含：

```text
RegisterBank
Rflags
CoreId
RIP
halted
```

`RFLAGS` 当前建模了 `CF/PF/ZF/SF/IF/OF`。`ADD/SUB/CMP`、`INC/DEC`、`AND/OR/XOR/TEST` 会按当前教学范围更新这些状态位，`Jcc/SETcc/CMOVcc` 会读取对应条件码。Stage 3 中 interrupt gate 会清 `IF`，`IRET/SYSRET` 会恢复保存的 flags。这比 RV 的显式寄存器比较更复杂，但更贴近 x86-64 现实。

CPU Stage 1 已经加入 `ExecutableImage -> Decoder -> Instruction` 的真实 byte image 路径，Stage 2 已补入 `CALL/RET/PUSH/POP`、`LEA`、逻辑/条件码和扩展 load。Stage 3 已加入：

```text
INT/INT3/IRET
SYSCALL/SYSRET
IDT/GDT/TSS 教学模型
TrapFrame + pending trap
CPL + IF
ThreadContext trapframe snapshot
```

Stage 4 已经把 paging/MMU/TLB 和 memory fault 接入 `TrapController`：`CpuState` 保存 CR3/CR2 风格状态，MMU 做 4-level walk/TLB 查询，缺页会产生 `#PF` trapframe 和 x86-64 page fault error code。Stage 5 直接复用这套 fault/trap 流程实现 OS 侧缺页处理、物理页分配器和进程地址空间。

Stage 5 已经完成第一版 OS 内存与调度底座：

```text
PhysicalPageAllocator  管理物理页、保留低端页、支持连续页和错误路径
AddressSpace           管理进程 page-table root/table arena，复用 CPU PageTableBuilder
PageFaultHandler       处理 not-present #PF，分配/清零/映射 demand page，失败回滚
Process                拥有进程 ID、地址空间和线程上下文
RoundRobinScheduler    管理 READY/RUNNING/BLOCKED/DEAD 的非拥有 run queue
Kernel syscall         RAX ABI，支持 YIELD/EXIT/unsupported result
```

Stage 6 已经完成第一版多核/原子底座：

```text
CoreTopology       固定核心数量和 bootstrap core
CpuState core id   CPU 状态可标记运行在哪个核心
Machine topology   platform facade 暴露 processor_count
LOCK CMPXCHG/XADD  x86-64 原子 RMW 教学指令
MFENCE             内存序屏障 ISA 钩子
X86 TSO            每核心 store buffer、load forwarding、fence/drain、locked CAS/fetch-add
```

Stage 7 已经完成第一版中断/抢占/TLB shootdown 底座：

```text
Local APIC/IOAPIC  per-core timer、IPI、external IRQ route/mask/unmask
Timer interrupt    Kernel tick 递增、唤醒 sleepers、触发 round-robin preempt
SleepQueue         按 tick 排序的非拥有 sleep queue，过期后交给 scheduler wake
WaitQueue          事件等待/唤醒队列，不绑定具体 scheduler 策略
PCID               PagingState 支持 12-bit x86 PCID 和 CR3 flush/preserve 模式
INVLPG             真实 byte encoding 0F 01 /7 m，ring0 下失效当前 PCID 的 TLB page
TLB shootdown      request/take/apply/ack 控制器，配合 tlb-shootdown IPI
Scheduler handoff  记录 source core、target core、thread id，并发送 reschedule IPI
```

Stage 8 已经完成第一版性能硬件底座：

```text
SetAssociativeCache     L1I/L1D cache，line/set/associativity/LRU/dirty eviction
InOrderPipeline         retire、branch redirect flush、exception flush 的确定性周期模型
PerformanceCounters     cycles、retired、cache hit/miss、TLB hit/miss、branch、flush、stall
Stage8PerformanceModel  Executor/MMU 可选性能 facade，未启用时不改变原语义
Benchmark               Stage8 performance model 已进入 benchmark smoke
```

## 下一步

合理顺序：

```text
1. per-core run queue、真实 SMP scheduler 和负载迁移
2. 用户态 loader、内核/用户地址布局、COW fork
3. signal/event/futex 风格等待语义
4. fs/block device/network
5. cache coherence / branch predictor / uop cache / SIMD
6. HPC/SIMD/AI 路线
```

这样学习者能从真实 x86-64 的 CPU 状态走到现代 OS，而不是只看抽象 API。
