# MNOS 架构说明

MNOS 的目标是做一个现代 x86-64 CPU 与计算机硬件模拟器，再在这个硬件底座上逐步实现现代 OS。项目后期会面向高性能计算、分布式网络、高性能网络、AI 推理/训练等方向，所以主线 ISA 采用 x86-64：复杂度更高，但更贴近当前服务器、工作站和高性能软件优化的现实。

当前已经完成 x86-64 Stage 1 基础底座：Stage 0 对象级 `Program` 路径继续保留用于教学和语义测试，Stage 1 新增真实 byte image fetch/decode 并复用同一套执行语义。

```text
寄存器    RAX/RBX/RCX/RDX/RSI/RDI/RBP/RSP/R8..R15
状态      RIP、RFLAGS(CF/ZF/SF/OF)、halted
数据宽度  BYTE/WORD/DWORD/QWORD
操作数    none/register/immediate/memory(base/index/scale/displacement/absolute + size)
指令      MOV/ADD/SUB/CMP/JMP/JE/JNE/HLT
内存      小端 PhysicalMemory + MemoryBus
decode    ExecutableImage + REX.W + ModRM/SIB + rel8/rel32 + RIP-relative
执行      单核 Executor::step/run，可选 ExecutionTrace，对象 Program 和 byte image 共用语义
```

## 当前目录

```text
include/mnos/
  core/                 EnumMap 等通用基础设施
  cpu/
    common/             x86-64 基础整数类型、DataSize
    decode/             ExecutableImage、Decoder、DecodedInstruction
    register/           16 个通用寄存器编号和 RegisterBank
    flags/              RFLAGS 状态位
    instruction/        Opcode、Operand、Instruction
    memory/             PhysicalMemory、MemoryBus
    execution/          CpuState、Program、ExecutionTrace、Executor
  os/
    platform/           Machine facade
    kernel/             BootContext、Kernel boot skeleton
    mm/                 PhysicalAddress、VirtualAddress、4KiB page 工具
    sched/              ThreadId、ThreadState、ThreadContext
```

依赖方向必须保持：

```text
mnos_emulator -> mnos_os -> mnos_cpu
benchmarks    -> mnos_os/mnos_cpu
tests         -> 被测目标
```

CPU core 不反向依赖 OS。CPU 负责 ISA、寄存器、RIP/RFLAGS、内存访问和执行语义；OS 负责 boot、线程、进程、地址空间、调度、文件系统、网络和系统调用。

## 为什么主线采用 x86-64？

采用 x86-64 的理由：

1. **现实高性能相关性强**：大量服务器、数据库、编译器、推理 runtime、网络栈、HPC 软件仍然深度围绕 x86-64 优化。
2. **学习价值高**：RIP-relative addressing、RFLAGS、variable-length decode、cache/TLB、SIMD/AVX、syscall ABI 都是现实系统优化的重要内容。
3. **OS 连接直接**：分页、异常、中断、系统调用、APIC、TSC、上下文切换等概念可以连接到真实 Linux/现代内核经验。
4. **复杂但值得**：x86-64 历史包袱多，但正因为复杂，学习者能更早理解真实工程里的兼容性、抽象边界和性能权衡。

这个选择意味着路线要更严谨：Stage 0 先用对象级指令保护语义和测试，Stage 1 再做真实机器码 fetch/decode。不能一开始就把 variable-length decode、分页、设备、多核全部塞进一个执行器。

## 当前 CPU Stage 0/1

Stage 0 对象级执行器仍然保留：

```text
Program = vector<Instruction>
RIP     = 当前对象指令槽位
HLT     = 停止执行
```

它的作用是让初学者先看清寄存器、RFLAGS、操作数、内存、跳转和测试。Stage 1 在这个语义底座上加入真实 x86-64 byte image：

```text
ExecutableImage = 机器码 byte stream + base RIP
RIP             = 当前 byte 地址
Decoder         = byte -> Instruction + next RIP
Executor        = decode 后复用 Stage 0 指令语义
```

当前语义：

```text
MOV dst, src      写寄存器或内存
ADD dst, src      写回 dst，更新 ZF/SF/CF/OF
SUB dst, src      写回 dst，更新 ZF/SF/CF/OF
CMP left, right   只更新 flags，不写回
JMP target        无条件跳转
JE/JNE target     基于 ZF 条件跳转
HLT               停止执行
```

当前 Stage 1 支持的机器码编码：

```text
REX.W
MOV r64, imm64                 B8+rd
MOV r/m64, r64                 89 /r
MOV r64, r/m64                 8B /r
ADD/SUB/CMP r/m64, r64         01/29/39 /r
ADD/SUB/CMP r64, r/m64         03/2B/3B /r
ADD/SUB/CMP r/m64, imm32       81 /0, /5, /7
JMP rel8/rel32                 EB/E9
JE/JNE rel8                    74/75
JE/JNE rel32                   0F 84/0F 85
HLT                            F4
```

ModRM、SIB、disp8、disp32、base+displacement、base+index*scale+displacement、index*scale+displacement、absolute displacement 和 RIP-relative addressing 都已有测试覆盖。当前非法或暂不支持的编码会抛出 `DecodeError`；后续 exception 阶段会把它接入 CPU fault/trap 流程。

执行器热路径保持低开销：

```text
Executor::step
  -> Program::instruction_at(rip) 或 Decoder::decode(image, rip)
  -> switch (Opcode)
  -> CpuState / MemoryBus
```

没有 per-instruction 虚函数分发，也没有每条指令堆分配。`Operand` 使用 `std::variant` 是为了清晰表达寄存器、立即数和内存操作数的合法形状；执行热路径仍是直接 switch。

## 当前 OS Stage 0

OS 目前建立硬件和 kernel 的最小边界：

```text
Machine       = PhysicalMemory + MemoryBus
BootContext   = kernel 启动资源视图
Kernel        = boot 状态机
Address/Page  = 物理/虚拟地址和 4KiB page 工具
ThreadContext = CpuState + kernel stack + ThreadState
```

`ThreadContext` 会把 `RSP` 初始化到 kernel stack top。后续 context switch、trapframe、syscall 都会建立在这个 CPU 上下文保存模型上。

## 工程原则

1. x86-64 主线不再混入 RV 指令、RV 寄存器名或 RV privileged 术语。
2. 复杂模块要分层：decode、execute、memory、device、OS 不互相吞并。
3. 设计模式只用于降低真实耦合：Facade、Adapter、Strategy、Builder、Factory、Observer、Value Object。
4. 热路径优先连续数据、表驱动、直接分派和可测量的低开销接口。
5. 每个阶段必须有 unit/integration/chaos/fuzz/benchmark/docs 支撑。

## 后续路线图

### Stage 1: x86-64 fetch/decode 已完成基础底座

```text
机器码 byte stream
variable-length instruction decoder
REX prefix
ModRM/SIB
immediate/displacement
RIP-relative addressing
DecodeError 非法编码入口
```

下一步不应把整个 x86-64 ISA 一次性塞进 decoder，而是沿 Stage 2 补齐整数指令，同时保持每个新增编码都有 unit/integration/benchmark/docs 支撑。

### Stage 2: 更完整的整数 ISA

```text
PUSH/POP/CALL/RET
LEA
INC/DEC/AND/OR/XOR/TEST
SETcc/CMOVcc
更多条件跳转
符号/零扩展 load
```

### Stage 3: exception、interrupt、syscall

```text
IDT/GDT/TSS 教学模型
异常和 fault
SYSCALL/SYSRET 或教学等价路径
timer interrupt
APIC/IOAPIC 教学模型
```

### Stage 4: x86-64 paging/MMU/TLB

```text
4-level/5-level page table 路线
PTE flags
page fault
TLB + shootdown
kernel/user address space
physical page allocator
```

### Stage 5: 线程、进程、scheduler

```text
trapframe
context switch
run queue
process + address space
syscall ABI
sleep/wait/exit/yield
```

### Stage 6: 原子操作、多核、内存模型

```text
LOCK 前缀教学模型
CMPXCHG/XADD
x86 TSO
multi-core topology
IPI
deterministic replay
race/order tests
```

### Stage 7: cache、pipeline、性能计数

```text
I-cache/D-cache
cache line
writeback/write-through
MESI/MOESI 教学模型
branch predictor
uop/decoder 教学抽象
perf counters
```

### Stage 8: 文件系统、块设备、高性能网络

```text
block device + DMA
buffer cache
VFS + 简化文件系统
NIC descriptor ring
interrupt vs polling
zero-copy
高性能网络 benchmark
```

### Stage 9: HPC、SIMD、AI 推理/训练

```text
SSE/AVX/AVX2/AVX-512 教学模拟路线
NUMA/topology
SIMD microkernel
memory bandwidth benchmark
AI operator benchmark
end-to-end trace/profiling
```

## 验证命令

```text
cmake --build builds/debug --parallel
ctest --test-dir builds/debug --output-on-failure
cmake --build builds/debug --target mnos_benchmark_smoke
builds/debug/mnos_emulator
```
