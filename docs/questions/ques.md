# C++ 与 MNOS 学习问题合集

这份文档把 C++ 语言规则和当前 x86-64 Stage 0..7 模型连起来。示例使用 `RIP/RFLAGS/Operand/MOV/HLT/TrapFrame/CoreTopology/LOCK/APIC/INVLPG` 等当前主线术语。

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

`Operand` 使用 `std::variant`，是为了把合法 payload 绑定到类型上，而不是用一堆松散字段。Stage 6 的 `CMPXCHG/XADD` 和 Stage 7 的 `INVLPG` 仍复用同一套 `Operand`，避免原子和 TLB 指令另建一套重复表示。

## 5. RIP 和 Program

真实 x86-64 的 `RIP` 是字节地址，指令长度可变。Stage 0 的 `Program` 仍然是对象指令数组，`RIP` 表示当前对象指令槽位；Stage 1 新增 `ExecutableImage`，`RIP` 表示当前 byte 地址；Stage 2 在同一套语义上加入栈、条件码和更多整数指令；Stage 3 加入 `INT/SYSCALL/SYSRET/IRET` 和 trapframe；Stage 6 加入 `LOCK CMPXCHG/XADD` 和 `MFENCE`；Stage 7 加入 `INVLPG`、APIC timer/IPI 和 TLB shootdown。

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

IOAPIC 负责外部 IRQ 路由，IPI 负责核心之间的通知。当前项目把这些做成确定性教学模型，后续才能自然扩展到 per-core run queue、负载迁移和真实设备中断。

## 11. DataSize

x86-64 常用数据宽度：

```text
BYTE   8 bit
WORD   16 bit
DWORD  32 bit
QWORD  64 bit
```

当前内存读写按小端序实现。Stage 1 已经支持 QWORD 级 ModRM/SIB/RIP-relative 访问；Stage 2 已加入 r/m8、r/m16、r/m32 source 宽度和 `MOVSX/MOVZX/MOVSXD`。后续 MMU/page fault 阶段会把这些访问接入地址转换和异常流。

## 12. 为什么热路径不用复杂模式？

`Executor` 是性能热点。每条指令都走虚函数和堆对象，会让后续性能研究失真。

当前设计：

```text
Program 连续存储
switch (Opcode)
Operand variant 表达形态
MemoryBus facade 隔离内存系统
ExecutionTrace 可选
```

Strategy、Adapter、Builder 等模式应该放在 ISA decoder、设备、平台配置、调度策略这些变化边界上，而不是塞进每条指令执行。

## 13. 下一阶段学习重点

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
SSE/AVX performance path
```
