# MNOS 架构说明

MNOS 的目标是做一个现代 x86-64 CPU 与计算机硬件模拟器，再在这个硬件底座上逐步实现现代 OS。项目后期会面向高性能计算、分布式网络、高性能网络、AI 推理/训练等方向，所以主线 ISA 采用 x86-64：复杂度更高，但更贴近当前服务器、工作站和高性能软件优化的现实。

当前已经完成 x86-64 Stage 5 CPU/OS 底座：Stage 0 对象级 `Program` 路径继续保留用于教学和语义测试，Stage 1 新增真实 byte image fetch/decode，Stage 2 在同一套执行语义上加入栈、条件码、逻辑指令和扩展 load，Stage 3 加入 IDT/GDT/TSS 教学模型、trapframe、软件中断和 syscall/sysret 控制流，Stage 4 加入 paging/MMU/TLB 与 page fault 接入，Stage 5 加入物理页分配、进程地址空间、缺页处理、进程/线程编排、round-robin scheduler 和最小 syscall ABI。

```text
寄存器    RAX/RBX/RCX/RDX/RSI/RDI/RBP/RSP/R8..R15
状态      RIP、RFLAGS(CF/PF/ZF/SF/IF/OF)、CPL、CR2/CR3 风格 paging state、halted、pending trap
数据宽度  BYTE/WORD/DWORD/QWORD
操作数    none/register/immediate/memory(base/index/scale/displacement/absolute + size)
指令      MOV/MOVSX/MOVZX/LEA/ADD/SUB/CMP/INC/DEC/AND/OR/XOR/TEST/PUSH/POP/CALL/RET/JMP/Jcc/SETcc/CMOVcc/INT/SYSCALL/SYSRET/IRET/HLT
内存      小端 PhysicalMemory + MemoryBus + PagingState + page table + TLB + MMU
decode    ExecutableImage + REX.W + ModRM/SIB + rel8/rel32 + RIP-relative + r/m8/r/m16/r/m32 source widths
系统表    PrivilegeLevel + GDT + TSS + IDT + TrapFrame + TrapController
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
    memory/             PhysicalMemory、MemoryBus、PagingState、PageTableEntry、PageTableBuilder、TLB、MMU
    system/             CPL、GDT、TSS、IDT、TrapFrame、TrapController
    execution/          CpuState、Program、ExecutionTrace、Executor
  os/
    platform/           Machine facade
    kernel/             BootContext、Kernel、syscall ABI
    mm/                 PhysicalAddress、VirtualAddress、4KiB page 工具、PhysicalPageAllocator、AddressSpace、PageFaultHandler
    proc/               ProcessId、Process
    sched/              ThreadId、ThreadState、ThreadContext、RoundRobinScheduler
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

## 当前 CPU Stage 0/1/2/3/4

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

Stage 0/1/2 当前语义：

```text
MOV dst, src        写寄存器或内存
MOVSX/MOVZX dst,src 符号/零扩展 BYTE/WORD/DWORD source 到 64-bit 目的寄存器
LEA dst, mem        计算有效地址，不读取内存
ADD/SUB/CMP         更新 CF/PF/ZF/SF/OF，其中 CMP 不写回
INC/DEC             更新 PF/ZF/SF/OF，保留 CF
AND/OR/XOR/TEST     更新 PF/ZF/SF，清 CF/OF，其中 TEST 不写回
PUSH/POP            64-bit 栈向低地址增长，通过 RSP 读写 QWORD
CALL/RET            CALL push 返回 RIP 后跳转，RET pop 返回 RIP
JMP/Jcc             无条件和条件跳转，条件码基于 RFLAGS
SETcc/CMOVcc        条件写 byte 或条件移动 64-bit 值
HLT                 停止执行
```

Stage 3 当前语义：

```text
CPL                 CpuState 保存当前 privilege level，默认 ring0
IF                  interrupt gate 清 IF，trap gate 保持 IF
GDT                 SegmentSelector + code/data/TSS descriptor 教学模型
TSS                 保存 ring0/ring1/ring2 privilege stack，当前主要用于 ring3 -> ring0 trap stack switch
IDT                 256 vector 固定表，vector -> interrupt/trap gate -> handler RIP
TrapFrame           保存 kind/vector/interrupted RIP/return RIP/RFLAGS/RSP/CPL/error code
INT imm8/INT3       进入 IDT software interrupt gate，return RIP 是下一条指令
IRET                从 pending TrapFrame 恢复 RIP/RFLAGS/RSP/CPL
SYSCALL             保存 RCX=return RIP、R11=RFLAGS，进入 MSR 风格 syscall entry
SYSRET              从 RCX/R11 和 pending syscall frame 返回用户态
ThreadContext       可 snapshot pending TrapFrame 为 last trapframe，供 scheduler/syscall 后续使用
```

Stage 4 当前语义：

```text
PagingState         CpuState 保存 CR3(root table)、CR2(fault linear address)、enabled、CR0.WP 教学位、NXE 教学位
PageTableEntry      x86-64 PTE/PDE/PDPTE/PML4E present/RW/US/A/D/PS/NX/address flags
PageTableBuilder    Builder 模式构造 4KiB/2MiB/1GiB 映射，后续 OS address space 可复用
PageTableWalker     4-level walk，支持 4KiB page、2MiB large page、1GiB large page
TLB                 固定容量 TranslationLookasideBuffer，CR3/paging generation 自动失效，显式 clear/invalidate_page
MMU                 统一 read/write/execute translation，处理跨 4KiB 页的 QWORD 访问
A/D bits            page walk 设置 accessed，write 设置 dirty，TLB 写命中补 dirty
权限                user/supervisor、read-only、CR0.WP、NX execute-disable、reserved bit
#PF                 PageFault -> TrapController::raise_exception(#PF, error_code)，CR2 保存 fault linear address
```

当前 `ExecutableImage` 仍是教学用 byte stream，不直接从物理内存取机器码；Stage 4 已对 decoded instruction 的 RIP 范围做 execute permission 检查。后续统一 instruction fetch/cache 时，会把 byte source 从 `ExecutableImage` 迁到可分页的内存视图。

当前 Stage 1/2 支持的机器码编码：

```text
REX.W
MOV r64, imm64                 B8+rd
MOV r/m64, r64                 89 /r
MOV r64, r/m64                 8B /r
MOVSX/MOVZX r64, r/m8/r/m16    0F BE/BF, 0F B6/B7
MOVSXD r64, r/m32              63 /r
LEA r64, m                     8D /r
ADD/SUB/CMP r/m64, r64         01/29/39 /r
ADD/SUB/CMP r64, r/m64         03/2B/3B /r
AND/OR/XOR/TEST r/m64, r64     21/09/31/85 /r
AND/OR/XOR r64, r/m64          23/0B/33 /r
ADD/OR/AND/SUB/XOR/CMP imm32   81 /0, /1, /4, /5, /6, /7
TEST r/m64, imm32              F7 /0
INC/DEC/CALL/PUSH r/m64        FF /0, /1, /2, /6
PUSH/POP r64                   50+rd, 58+rd
PUSH imm8/imm32                6A, 68
POP r/m64                      8F /0
CALL rel32                     E8
RET                            C3
JMP rel8/rel32                 EB/E9
Jcc rel8/rel32                 70..7F, 0F 80..8F
SETcc r/m8                     0F 90..9F
CMOVcc r64, r/m64              0F 40..4F
INT3                           CC
INT imm8                       CD ib
IRET                           CF
SYSCALL                        0F 05
SYSRETQ                        REX.W 0F 07
HLT                            F4
```

ModRM、SIB、disp8、disp32、base+displacement、base+index*scale+displacement、index*scale+displacement、absolute displacement 和 RIP-relative addressing 都已有测试覆盖。当前非法或暂不支持的编码仍会抛出 `DecodeError`；分页开启后，内存读写和取指权限检查会把 `PageFault` 接入同一套 CPU fault/trap 流程。

执行器热路径保持低开销：

```text
Executor::step
  -> Program::instruction_at(rip) 或 Decoder::decode(image, rip)
  -> switch (Opcode)
  -> CpuState / MemoryBus
```

没有 per-instruction 虚函数分发，也没有每条指令堆分配。`Operand` 使用 `std::variant` 是为了清晰表达寄存器、立即数和内存操作数的合法形状；执行热路径仍是直接 switch。

## 当前 OS Stage 0/5

OS 先建立硬件和 kernel 的最小边界：

```text
Machine       = PhysicalMemory + MemoryBus
BootContext   = kernel 启动资源视图
Kernel        = boot 状态机
Address/Page  = 物理/虚拟地址和 4KiB page 工具
ThreadContext = CpuState + kernel stack + ThreadState
```

`ThreadContext` 会把 `RSP` 初始化到 kernel stack top，并能保存 CPU pending trapframe 的快照。Stage 4 已经让 page fault 进入 `TrapController`，Stage 5 在此基础上补齐了 OS 侧内存和调度底座：

```text
PhysicalPageAllocator  bitmap 风格物理页分配，支持单页/连续页、保留页和错误路径
AddressSpace           拥有 page-table root/table arena 状态，通过 PageTableBuilder 建立映射并激活 CR3
PageFaultHandler       处理 not-present #PF，分配并清零物理页，建立 demand mapping，失败时回滚
Process                拥有一个 AddressSpace 和稳定地址的 ThreadContext deque
RoundRobinScheduler    非拥有调度队列，READY/RUNNING/BLOCKED/DEAD 状态转换
Syscall ABI            RAX 传 syscall number，支持 YIELD/EXIT 和 unsupported result
Kernel                 boot 后编排 allocator/address space/process/thread/scheduler/page fault/syscall
```

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

后续仍不应把整个 x86-64 ISA 一次性塞进 decoder。Stage 5 已经把进程地址空间、page fault handler、物理页分配器和 scheduler 行为接入 Kernel；下一步应沿原子操作、多核、APIC/timer 和 TLB shootdown 推进，每个新增硬件行为都要有 unit/integration/benchmark/docs 支撑。

### Stage 2: 更完整的整数 ISA 已完成当前教学范围

```text
PUSH/POP/CALL/RET
LEA
INC/DEC/AND/OR/XOR/TEST
SETcc/CMOVcc
更多条件跳转
符号/零扩展 load
```

### Stage 3: exception、interrupt、syscall 已完成基础底座

```text
IDT/GDT/TSS 教学模型
TrapFrame + pending trap
INT/INT3/IRET
SYSCALL/SYSRET
IF 与 ring0 stack switch
ThreadContext trapframe snapshot
```

### Stage 4: x86-64 paging/MMU/TLB 与 page fault 已完成当前底座

```text
4-level page table
PTE/PDE/PDPTE/PML4E flags
4KiB/2MiB/1GiB mapping
CR3/CR2 style CPU state
TLB + explicit invalidation
MMU read/write/execute translation
cross-page memory access
page fault error code
TrapController #PF integration
PageTableBuilder for future OS address spaces
```

Stage 4 仍刻意不做全部内存系统：5-level paging、PCID、INVLPG 指令编码、TLB shootdown、多核一致性、物理页分配器、demand paging 和 COW 放到 Stage 5/6 与进程/多核一起推进。其中物理页分配器、进程地址空间、基础 demand paging 已在 Stage 5 完成，COW、PCID、INVLPG 和 shootdown 继续留到多核阶段。

### Stage 5: 线程、进程、scheduler 已完成当前底座

```text
physical page allocator
process + address space
page fault handler
run queue
round-robin scheduler
syscall ABI: yield/exit/unsupported
kernel process/thread creation facade
unit/integration/chaos/fuzz/benchmark/docs
```

Stage 5 仍不假装已经完成完整现代内核：真实 timer tick、抢占式 context switch、用户态 ELF loader、COW fork、wait/sleep 队列、信号、APIC/IOAPIC、PCID/INVLPG/TLB shootdown、多核同步都留给 Stage 6+。

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
