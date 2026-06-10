# C++ 与 MNOS 学习问题合集

这份文档把 C++ 语言规则和当前 x86-64 Stage 0..9 模型连起来。示例使用 `RIP/RFLAGS/Operand/MOV/HLT/TrapFrame/CoreTopology/LOCK/APIC/INVLPG/Cache/Pipeline/PerfCounter/SmpScheduler` 等当前主线术语。

## 1. 编译期常量和运行时常量

编译期常量能在编译阶段确定：

```cpp
constexpr std::size_t REGISTER_COUNT = 16;
std::array<cpu::Qword, REGISTER_COUNT> registers{};
```

运行时常量初始化后不能修改，但值到运行时才知道：

```cpp
const std::size_t memory_size = read_memory_size();
cpu::PhysicalMemory memory(memory_size);
```

`const` 表示对象不能通过这个名字修改；`constexpr` 表示值必须能编译期求出。

## 2. `std::string_view`

`std::string_view` 是不拥有内存的视图：

```cpp
std::string_view name = cpu::opcode_to_assembly_name(cpu::Opcode::MOV);
```

项目里的 opcode/register/flag 名称来自静态表，生命周期足够长，返回 view 没问题。

## 3. `enum class`

硬件概念不能随便混用，所以用强类型枚举：

```cpp
enum class Opcode : std::uint8_t
{
    MOV,
    ADD,
    HLT,
    COUNT
};
```

`Opcode`、`RegisterId`、`FlagId`、`DataSize` 是不同类型，编译器会帮我们阻止误用。

## 4. 为什么要有 Operand？

x86-64 指令有寄存器、立即数、内存等操作数形态：

```text
MOV RAX, 42
MOV [RBP - 8], RAX
ADD RAX, [RBX + 16]
```

当前用 `Operand` 建模：

```text
none
register
immediate
memory(base/index/scale/displacement/absolute + data size)
```

`Operand` 使用 `std::variant`，是为了把合法 payload 绑定到类型上，而不是用一堆松散字段。Stage 6 的 `CMPXCHG/XADD` 和 Stage 7 的 `INVLPG` 仍复用同一套 `Operand`，Stage 8 的 cache/pipeline/perf hook 也观察同一套执行路径，避免原子、TLB 和性能模型各自另建重复表示。

## 5. RIP 和 Program

真实 x86-64 的 `RIP` 是字节地址，指令长度可变。Stage 0 的 `Program` 仍然是对象指令数组，`RIP` 表示当前对象指令槽位；Stage 1 新增 `ExecutableImage`，`RIP` 表示当前 byte 地址；Stage 2 在同一套语义上加入栈、条件码和更多整数指令；Stage 3 加入 `INT/SYSCALL/SYSRET/IRET` 和 trapframe；Stage 4 加入 paging/MMU/TLB/page fault；Stage 5 加入第一版 OS 进程、地址空间、调度和 syscall ABI；Stage 6 加入 `LOCK CMPXCHG/XADD` 和 `MFENCE`；Stage 7 加入 `INVLPG`、APIC timer/IPI 和 TLB shootdown；Stage 8 加入可选 instruction fetch cache、pipeline retire 和 perf counter 统计；Stage 9 加入 per-core run queue、SMP scheduler 和负载迁移。

当前 byte image 路径是：

```text
RIP -> ExecutableImage 取 byte -> variable-length decoder -> Instruction -> Executor
```

对象路径和 byte 路径复用同一套 `Instruction` 执行语义，这样可以先稳定 RFLAGS、内存、栈和跳转，再逐步扩展更多 x86-64 编码。

## 6. RFLAGS

x86-64 普通整数指令会更新隐式状态位：

```text
ADD/SUB/CMP      -> CF/PF/ZF/SF/OF
INC/DEC          -> PF/ZF/SF/OF，保留 CF
AND/OR/XOR/TEST  -> PF/ZF/SF，清 CF/OF
Jcc/SETcc/CMOVcc -> 读取条件码
```

当前 `Rflags` 建模：

```text
CF Carry
PF Parity
ZF Zero
SF Sign
IF Interrupt Enable
OF Overflow
```

这对学习高性能代码很重要，因为很多分支、比较、条件移动、原子操作都会和 flags 相关。

## 7. LOCK、CMPXCHG 和 XADD

x86-64 的原子 RMW 指令不是普通函数调用，而是 ISA 级别的内存顺序和缓存一致性入口。当前 Stage 6 支持：

```text
LOCK CMPXCHG r/m64, r64
LOCK XADD r/m64, r64
MFENCE
```

`CMPXCHG` 用 `RAX` 和目的值比较，成功则写入 source，失败则把目的值写回 `RAX`，并按一次减法更新 `RFLAGS`。`XADD` 会把目的值和 source 相加，source 得到旧目的值，目的位置得到加法结果。`LOCK` 当前只允许内存目的操作数，这符合真实 x86-64 的核心约束，也避免学习者误以为寄存器上的 `LOCK` 有意义。

## 8. PCID、INVLPG 和 TLB shootdown 是什么？

TLB 是 CPU 缓存虚拟地址到物理地址翻译结果的结构。现代 OS 修改页表后，不能只改内存里的 page table，还要让相关核心丢掉旧翻译。

当前 Stage 7 建模：

```text
ProcessContextId        12-bit x86 PCID，区分不同地址空间的 TLB entry
INVLPG m                失效当前 PCID 下覆盖 m 线性地址的 TLB page
TlbShootdownRequest     source core -> target core 的失效请求
tlb-shootdown IPI       通知目标核心本地 apply 并 ack
```

这让学习者能看到现代内核为什么需要“改页表 + 本地 INVLPG + 远端 shootdown IPI”，而不是把页表当普通 map 改完就结束。

## 9. TrapFrame 是什么？

现代 OS 不是直接“调用内核函数”，而是 CPU 先把硬件现场保存起来，再跳到内核入口。当前 Stage 3 用 `TrapFrame` 表达：

```text
trap kind
vector
interrupted RIP
return RIP
RFLAGS
RSP
CPL
optional error code
```

`INT/INT3` 通过 IDT 进入 handler，`IRET` 从 `TrapFrame` 恢复；`SYSCALL` 保存 `RCX/R11` 并进入 syscall entry，`SYSRET` 返回。`ThreadContext` 可以保存最后一次 pending trapframe 的快照；Stage 5 已把这条路径接到 scheduler、syscall ABI 和 page fault handler 的第一版 OS 底座。

## 10. APIC timer 为什么重要？

协作式调度只在 syscall/yield 时切换线程；现代 OS 需要 timer interrupt 抢占正在运行的线程。

Stage 7 的路径是：

```text
LocalApicTimer tick
  -> timer interrupt
  -> Kernel scheduler tick
  -> SleepQueue 唤醒过期线程
  -> RoundRobinScheduler preempt/yield current
```

Stage 9 之后，SMP 路径会把 timer interrupt 绑定到目标 core 的 run queue：

```text
LocalApicTimer tick(core N)
  -> Kernel SMP timer
  -> SmpScheduler core N tick/preemption
  -> 只 yield/schedule core N 的 current
```

IOAPIC 负责外部 IRQ 路由，IPI 负责核心之间的通知。当前项目把这些做成确定性教学模型，后续才能自然扩展到更真实的调度策略和设备中断。

## 11. cache、pipeline 和 perf counter 为什么重要？

同一段代码在现代 CPU 上快慢差异很大，原因经常不是“指令条数”本身，而是 cache miss、TLB miss、branch redirect、pipeline stall、dirty eviction 等硬件事件。

Stage 8 当前建模：

```text
L1I/L1D cache       组相联、cache line、LRU victim
write policy       write-back / write-through
pipeline           retire cycle、branch redirect flush、exception flush
perf counter       cycles、retired、cache hit/miss、TLB hit/miss、branch、stall
```

`Stage8PerformanceModel` 是显式启用的可选 facade。未启用时 Executor/MMU 行为不变；启用后，Executor 记录 instruction fetch、retire 和 branch redirect，MMU 记录 TLB hit/miss 和 D-cache 读写。这让学习者能把算法性能和硬件现象连起来，而不是只看抽象 Big-O。

## 12. SMP scheduler 为什么需要 per-core run queue？

现代多核 OS 不应该让所有核心都抢一个全局队列。per-core run queue 能减少共享状态，让 timer tick、wake、迁移和负载均衡都围绕具体 core 发生。

Stage 9 当前建模：

```text
SmpScheduler        每核心 run queue/current
create_thread_on_core 线程进入指定 core 的 ready queue
cross-core wake    wake 到目标 core，并发送 reschedule IPI
ready migration    只迁移 queued READY thread
rebalance_once     从最忙 ready queue 移动一个 runnable thread 到最轻 core
TLB local apply    目标 core take/apply/ack shootdown request
```

这里故意不迁移正在运行的线程。真实内核要先让目标线程离开 CPU 或通过 IPI/抢占进入安全点，再切换运行位置。Stage 9 先把这个约束显式做出来，避免学习者误以为“把 core_id 改掉”就等于完成线程迁移。

## 13. DataSize

x86-64 常用数据宽度：

```text
BYTE   8 bit
WORD   16 bit
DWORD  32 bit
QWORD  64 bit
```

当前内存读写按小端序实现。Stage 1 已经支持 QWORD 级 ModRM/SIB/RIP-relative 访问；Stage 2 已加入 r/m8、r/m16、r/m32 source 宽度和 `MOVSX/MOVZX/MOVSXD`。Stage 4 已把这些访问接入 MMU 地址转换和 page fault 异常流，Stage 8 再在 MMU 读写路径上记录 D-cache 与 TLB 性能事件。

## 14. 为什么热路径不用复杂模式？

`Executor` 是性能热点。每条指令都走虚函数和堆对象，会让后续性能研究失真。

当前设计：

```text
Program 连续存储
switch (Opcode)
Operand variant 表达形态
MemoryBus facade 隔离内存系统
ExecutionTrace 可选
Stage8PerformanceModel 可选
```

Strategy、Adapter、Builder 等模式应该放在 ISA decoder、设备、平台配置、调度策略这些变化边界上，而不是塞进每条指令执行。

## 15. 下一阶段学习重点

```text
x86-64 instruction byte encoding
REX prefix
ModRM/SIB
RIP-relative addressing
IDT/GDT/TSS/trapframe
4-level paging + page fault
LOCK/atomic and x86 TSO
timer/APIC/IPI/INVLPG/TLB shootdown
cache/pipeline/perf counter
per-core run queue / SMP scheduler
user loader / kernel-user address layout
COW fork / futex / event
ELF64 exec file path / argc argv envp user ABI
SSE/AVX performance path
```
