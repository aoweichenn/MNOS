# MNOS 架构说明

MNOS 的目标是做一个现代 x86-64 CPU 与计算机硬件模拟器，再在这个硬件底座上逐步实现现代 OS。项目后期会面向高性能计算、分布式网络、高性能网络、AI 推理/训练等方向，所以主线 ISA 采用 x86-64：复杂度更高，但更贴近当前服务器、工作站和高性能软件优化的现实。

当前已经完成 x86-64 Stage 15D CPU/OS 底座：Stage 0 对象级 `Program` 路径继续保留用于教学和语义测试，Stage 1 新增真实 byte image fetch/decode，Stage 2 在同一套执行语义上加入栈、条件码、逻辑指令和扩展 load，Stage 3 加入 IDT/GDT/TSS 教学模型、trapframe、软件中断和 syscall/sysret 控制流，Stage 4 加入 paging/MMU/TLB 与 page fault 接入，Stage 5 加入物理页分配、进程地址空间、缺页处理、进程/线程编排、round-robin scheduler 和最小 syscall ABI，Stage 6 加入 `LOCK` 原子指令、core topology 和 x86 TSO 教学内存模型，Stage 7 加入 local APIC/IOAPIC、timer interrupt、抢占 tick、sleep/wait queue、IPI、PCID/INVLPG/TLB shootdown 和多核心 scheduler handoff 入口，Stage 8 加入 L1I/L1D cache、in-order pipeline 和性能计数模型，Stage 9 加入 per-core run queue、SMP scheduler、跨核心 wake/reschedule、ready-thread migration、load rebalance 和 TLB shootdown 本地 apply 闭环，Stage 10 加入用户态地址布局、user loader、COW fork、futex 和 event 等用户进程运行语义，Stage 11 加入 x86-64 syscall ABI、用户 syscall/trap 完成、匿名页映射、COW/futex syscall 和用户 page fault 分流，Stage 12 加入文本终端设备、kernel console 和 TTY line discipline，Stage 13 加入进程 stdio fd 表、READ/WRITE syscall 到 TTY 的桥接，以及 shell parser/builtin/session，Stage 14 把 TTY 行输入、fd read blocking、prompt、pending line buffer 和 shell builtin 执行贯穿成可轮询的交互式 shell loop，Stage 15A 加入内存块设备、块几何校验和 write-back buffer cache，Stage 15B 在块缓存上加入 SimpleFS、inode/dirent 和 VFS file object，Stage 15C 把 root VFS 接入 fd table、文件 syscall 和 shell 文件命令，Stage 15D 加入 host 侧交互终端 adapter 和 `mnos_console`。

```text
寄存器    RAX/RBX/RCX/RDX/RSI/RDI/RBP/RSP/R8..R15
状态      RIP、RFLAGS(CF/PF/ZF/SF/IF/OF)、core id、CPL、CR2/CR3 风格 paging state、halted、pending trap
数据宽度  BYTE/WORD/DWORD/QWORD
操作数    none/register/immediate/memory(base/index/scale/displacement/absolute + size)
指令      MOV/MOVSX/MOVZX/LEA/ADD/SUB/CMP/INC/DEC/AND/OR/XOR/TEST/CMPXCHG/XADD/MFENCE/INVLPG/PUSH/POP/CALL/RET/JMP/Jcc/SETcc/CMOVcc/INT/SYSCALL/SYSRET/IRET/HLT
内存      小端 PhysicalMemory + MemoryBus + PagingState/PCID + page table + TLB + MMU + TLB shootdown + L1 cache + X86TsoMemoryModel
decode    ExecutableImage + REX.W + LOCK + ModRM/SIB + rel8/rel32 + RIP-relative + r/m8/r/m16/r/m32/r/m64 source widths + INVLPG
系统表    CoreId/CoreTopology + PrivilegeLevel + GDT + TSS + IDT + TrapFrame + TrapController + Local APIC/IOAPIC
执行      Executor::step/run，可选 ExecutionTrace/Stage8PerformanceModel，对象 Program 和 byte image 共用语义，原子指令仍保持 direct switch
```

## 当前目录

```text
include/mnos/
  core/                 EnumMap 等通用基础设施
  host/                 宿主机交互 adapter，例如 TerminalRunner
  cpu/
    common/             x86-64 基础整数类型、DataSize
    decode/             ExecutableImage、Decoder、DecodedInstruction
    register/           16 个通用寄存器编号和 RegisterBank
    flags/              RFLAGS 状态位
    instruction/        Opcode、Operand、Instruction
    memory/             PhysicalMemory、MemoryBus、Cache、PagingState/PCID、PageTableEntry、PageTableBuilder、TLB、MMU、TLB shootdown、X86TsoMemoryModel
    system/             CoreId、CoreTopology、CPL、GDT、TSS、IDT、TrapFrame、TrapController、Local APIC、IOAPIC
    execution/          CpuState、Program、ExecutionTrace、InOrderPipeline、Executor
    perf/               Stage8PerformanceModel、PerformanceCounters
  os/
    block/              BlockAddress、MemoryBlockDevice、BufferCache，后续 SimpleFS/VFS 的块存储地基
    dev/                TextDisplayBuffer、KeyboardInputQueue、TerminalDevice
    fs/                 SimpleFS format/mount、inode/dirent、VFS path facade、VfsFile 文件对象
    io/                 FileDescriptor、OpenFileDescription、FileDescriptorTable、IoResult
    platform/           Machine facade，持有内存、core topology 和 terminal device
    kernel/             BootContext、Kernel、syscall ABI
    mm/                 PhysicalAddress、VirtualAddress、4KiB page 工具、AddressLayout、PhysicalPageAllocator、AddressSpace、PageFaultHandler
    proc/               ProcessId、Process、process_context、UserProgram/UserLoader、CopyOnWriteManager、FutexTable
    sched/              ThreadId、ThreadState、ThreadContext、RoundRobinScheduler、SmpScheduler、SleepQueue、WaitQueue、Event
    shell/              ShellParser、ShellBuiltinRegistry、Shell、ShellSession 交互 loop、文件命令
    tty/                Console、TTY canonical line input、console read/write result
src/
  host/                 mnos_console 入口和宿主机终端 adapter 实现
```

依赖方向必须保持：

```text
mnos_emulator -> mnos_os -> mnos_cpu
mnos_console  -> mnos_host -> mnos_os -> mnos_cpu
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

## 当前 CPU Stage 0-8

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

Stage 6 当前语义：

```text
CoreId/CoreTopology  描述 bootstrap core 和固定核心数量，Machine 暴露 processor_count
CpuState             保存当前 core id，APIC/IPI/scheduler handoff 可把线程绑定到具体核心
LOCK prefix          Decoder 支持 legacy LOCK prefix，当前只允许内存目的的原子 RMW 编码
CMPXCHG r/m64,r64    比较 RAX 与目的值，成功写 source，失败把目的值写回 RAX，并按 SUB 更新 RFLAGS
XADD r/m64,r64       目的值与 source 相加，source 得到旧目的值，并按 ADD 更新 RFLAGS
MFENCE               作为 ISA 屏障钩子进入 Instruction/Executor，后续可接 store buffer drain
X86TsoMemoryModel    每核心 FIFO store buffer、本核心 load forwarding、fence/drain、locked CAS/fetch-add
```

Stage 7 当前语义：

```text
LocalApicTimer       one-shot/periodic timer，tick 到期后产生 timer interrupt
LocalApic            per-core interrupt queue，接收 timer/IPI/external IRQ
IoApic               IRQ line -> interrupt vector + target core route，支持 mask/unmask
ApicSystem           按 CoreTopology 组织 local APIC，支持 single/broadcast IPI 和 IOAPIC delivery
InterruptVector      新增 tlb-shootdown/reschedule 教学 vector
ProcessContextId     12-bit x86 PCID，PagingState 支持 CR3 load preserve/flush 语义
INVLPG               真实 0F 01 /7 m 编码，ring0 下按当前 PCID 失效对应 TLB 页，ring3 下进入 #GP
TLB                  支持按页、按 PCID、按地址空间和全量 flush
TlbShootdownController 记录 shootdown request，目标核心 apply 后 ack
SleepQueue/WaitQueue 非拥有等待队列，线程所有权仍在 Process/ThreadContext
Kernel timer tick    APIC timer -> scheduler tick -> wake sleepers -> round-robin preempt
Scheduler handoff    记录 source/target core/thread，并向目标 core 发送 reschedule IPI
```

Stage 8 当前语义：

```text
CacheGeometry          line size / set count / associativity，校验 power-of-two 几何
SetAssociativeCache    组相联 L1 cache，LRU victim，支持跨 cache line 访问统计
CacheWritePolicy       data cache 支持 write-back/write-through，记录 dirty eviction
InOrderPipeline        retire 周期、branch redirect flush、exception flush 的第一版流水线模型
PerformanceCounters    cycles、retired、I-cache/D-cache hit/miss、TLB hit/miss、branch、flush、stall
Stage8PerformanceModel 组合 L1I/L1D + pipeline + counters，作为 Executor/MMU 的可选性能 facade
Executor hook          byte/object 路径记录 instruction fetch、retire、branch redirect，不改变原执行语义
MMU hook               paging 路径记录 TLB hit/miss，读写路径按物理地址记录 D-cache 访问
```

Stage 9 当前语义：

```text
SmpScheduler           每核心 run queue/current，非拥有 ThreadContext 指针
per-core timer         每核心 tick/preemption 统计，目标 core 只抢占本地 current
cross-core wake        wake 到目标 core run queue，并通过 reschedule IPI 通知
ready migration        只迁移 queued READY thread，不伪造 running thread 热迁移
rebalance_once         从 ready 队列最重的 core 向最轻 core 迁移一个 runnable thread
Kernel Stage9 facade   create_thread_on_core、handle_smp_timer_interrupt、request_smp_migration
TLB shootdown apply    Kernel 可让目标 core take/apply/ack pending shootdown request
```

Stage 10 当前语义：

```text
AddressLayout          user low/text/heap/stack top 和 canonical kernel high 边界
UserProgram/UserLoader text/data/bss segment 权限、entry 校验、用户栈映射、ring3 RIP/RSP/PCID 初始化
AddressSpace           支持 PCID CR3 activate、page translation 查询、4KiB PTE 权限更新
CopyOnWriteManager     writable page fork 后降为 read-only，嵌套 fork 增加 frame ref，写 fault 时复制或最后引用恢复
FutexTable             process-scoped user futex word key，等待线程仍由 Process/ThreadContext 拥有
Event                  manual-reset event 风格等待对象，signal 后返回待调度线程
Kernel Stage10 facade  create_user_process、fork_process_cow、handle_cow_write_fault、wait/wake futex
```

Stage 11 当前语义：

```text
SyscallFrame          x86-64 syscall ABI：RAX number，RDI/RSI/RDX/R10/R8/R9 arguments，RAX result
Errno result ABI      成功返回非负值，失败返回负 errno，如 ENOSYS/EFAULT/EAGAIN/ENOMEM/EEXIST
Kernel user syscall   handle_user_syscall 校验 ring3 syscall trap、snapshot trapframe、分发后 SYSRET
Stage10 syscall hook  getpid、map anonymous page、fork COW range、futex wait/wake_one/wake_all
User page fault path  ring3 #PF 先尝试 COW write fault，再尝试 demand page，非法用户地址 kill thread
TrapController        restore_trap_frame 公共入口，SYSRET 前校验返回 RIP/RSP canonical
Process context       ProcessId -> PCID 规则集中到 process_context，UserLoader/Kernel 共用
```

Stage 12 当前语义：

```text
TextDisplayBuffer     文本显存 char/attribute、cursor、clear、scroll、render_text
KeyboardInputQueue    key event FIFO，支持容量限制、pop、drain
TerminalDevice        display + keyboard 的硬件 facade，由 Machine 持有
BootContext           暴露 terminal_device，kernel 和测试共享同一个硬件对象
Console write         kernel console 输出写入 TextDisplayBuffer
TTY canonical input   printable echo、backspace 删除、enter 提交整行，read 返回包含 newline 的字节
Blocking read         无完整行时线程进入 WaitQueue；输入完成一行后由 Kernel 唤醒 scheduler
Kernel Stage12 facade console_write、console_read、submit_terminal_input
```

Stage 13 当前语义：

```text
FileDescriptor        stdio value object，stdin=0/stdout=1/stderr=2，非法 fd 显式建模
FileDescriptorTable   每进程持有 fd table，默认把 stdin/stdout/stderr 接到 TTY，fork COW 时复制表并共享 open-file-description offset
READ/WRITE syscall    fd 校验、最大传输长度、用户地址范围和 MMU access range 校验、负 errno 返回
TTY fd bridge         stdin read 可阻塞当前线程，stdout/stderr write 写入 kernel console
ShellParser           空白分隔、单双引号、反斜杠转义、未闭合引号诊断
Builtin registry      help/clear/echo/ps/mem/cpu/ticks/exit 静态目录，函数指针分发，无虚调度
Shell session         消费 parser + registry，通过 kernel console 输出，为后续 stdout fd 替换保留清晰边界
Emulator smoke        启动时执行 shell echo，并打印 stage13=ready/shell_running=true
```

Stage 14 当前语义：

```text
ShellSession          持有 Shell + Kernel/Process/ThreadContext 引用，驱动交互式 shell loop
Prompt path           每轮读取前通过 stdout fd 写入 mnos>，不直接绕过 fd 桥接
Stdin path            通过 Kernel::read_fd 读取 stdin；无完整 TTY 行时返回 BLOCKED 并让 scheduler 阻塞当前线程
Pending line buffer   处理一次 read 返回半行、多行和 CRLF，保证命令不会因为固定读缓冲被截断
Step result           BLOCKED/PENDING_INPUT/COMMAND/EXITED/IO_ERROR 显式建模，便于 emulator、测试和未来 event loop 调度
Emulator smoke        先 poll 到 BLOCKED，再提交终端输入唤醒 shell 线程，最后执行 echo 并打印 stage14=ready
```

Stage 15A 当前语义：

```text
BlockAddress            块号值对象，避免和 byte offset/page number 混用
BlockDeviceGeometry     block size/block count/capacity 校验，拒绝零大小和 uint64 overflow
MemoryBlockDevice       内存后端块设备，支持整块和连续多块读写、clear、contains
BufferCache             固定容量 write-back 缓存，哈希索引命中路径 + LRU victim 选择
Dirty writeback         write miss 不额外读设备，flush/flush_all/dirty eviction 统一写回脏块
Cache stats             hit/miss、device read、write call、device writeback、dirty eviction
Benchmark smoke         MemoryBlockDevice read/write 与 BufferCache read-hit
```

Stage 15B 当前语义：

```text
SimpleFS format/mount    superblock、inode table、data bitmap、data block 区的真实块格式
Inode model              fixed inode record、file/directory kind、direct block、size metadata
Directory model          fixed dirent、root/nested directory、lookup、create_file、create_directory、read_directory
File I/O                 跨块 read/write、overwrite、append、direct-block 容量检查、拒绝 sparse gap
VFS facade               absolute path 解析、lookup/create/open_file/read_directory，不引入 syscall
VfsFile object           inode + open mode + offset，支持 read/write/seek 和 mode 校验
Benchmark smoke          SimpleFS file read/write
```

Stage 15C 当前语义：

```text
Kernel root VFS           boot 时构建内存块设备、buffer cache、SimpleFS 和 VFS root mount
Open file description     fd entry 共享 VfsFile offset，fork COW 复制 fd table 后保持现代 open-file-description 语义
VFS fd bridge             READ/WRITE syscall 通过 Kernel::read_fd/write_fd 访问 TTY 或 VFS_FILE
File syscall ABI          OPEN/CLOSE/STAT/READDIR，path 为 user address + length，stat/dirent 为固定小端 record
Shell file commands       ls/cat/touch/write/stat 通过 Kernel::vfs() 操作 root VFS
Benchmark smoke           Kernel VFS open/close fd
```

Stage 15D 当前语义：

```text
TerminalRunner            宿主机终端 adapter，读取 host stdin，提交到 Kernel::submit_terminal_input
Shell drive loop          复用 ShellSession::poll，驱动 prompt、blocking read、命令执行和 exit
Stream renderer           消费 TerminalDevice 增量输出流，默认不重复重放历史屏幕
Screen renderer           保留 TextDisplayBuffer 80x25 快照渲染，作为显式 screen 调试模式
mnos_console              可直接运行的交互入口，默认 ANSI stream，--plain 便于 pipe/测试输出
Host tests                用 istringstream/ostringstream 覆盖 help、文件命令、EOF 和 replay 回归
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

当前支持的机器码编码：

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
CMPXCHG r/m64, r64             0F B1 /r
XADD r/m64, r64                0F C1 /r
LOCK CMPXCHG r/m64, r64        F0 REX.W 0F B1 /r
LOCK XADD r/m64, r64           F0 REX.W 0F C1 /r
MFENCE                         0F AE F0
INVLPG m                       0F 01 /7
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
  -> 可选 Stage8 I-cache/pipeline/perf hook
  -> switch (Opcode)
  -> CpuState / MemoryBus / 可选 D-cache/TLB perf hook
```

没有 per-instruction 虚函数分发，也没有每条指令堆分配。Stage 8 性能模型是显式启用的可选 facade，未启用时 Executor/MMU 只做空指针检查；启用后用固定容量 cache、直接计数器和 in-order pipeline 统计支撑性能教学。`Operand` 使用 `std::variant` 是为了清晰表达寄存器、立即数和内存操作数的合法形状；执行热路径仍是直接 switch。

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

后续仍不应把整个 x86-64 ISA 一次性塞进 decoder。Stage 6 已经把原子操作、core topology 和 x86 TSO 教学模型接入主线；Stage 7 已经把 APIC/timer、抢占式调度、IPI、PCID/INVLPG 和 TLB shootdown 接入主线；Stage 8 已经把 cache、pipeline 和 perf counter 第一版接入主线；Stage 9 已经把 per-core run queue、SMP scheduler 和负载迁移接入主线。Stage 15D 已经补上宿主机交互终端入口；下一步应先考虑可选窗口终端 adapter，再沿 exec/wait、pipe/dup/redirect、page cache/mmap 和高性能网络等 OS 语义推进，每个新增硬件/OS 行为都要有 unit/integration/benchmark/docs 支撑。

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

Stage 4 仍刻意不做全部内存系统：5-level paging、PCID、INVLPG 指令编码、TLB shootdown、多核一致性、物理页分配器、demand paging 和 COW 放到 Stage 5+ 与进程/多核一起推进。其中物理页分配器、进程地址空间、基础 demand paging 已在 Stage 5 完成，PCID、INVLPG 和 shootdown 已在 Stage 7 完成当前教学底座，COW 和更完整的地址空间隔离继续留到后续 OS 阶段。

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

Stage 5 仍不假装已经完成完整现代内核：真实 timer tick、抢占式 context switch、wait/sleep 队列、APIC/IOAPIC、PCID/INVLPG/TLB shootdown 已在 Stage 7 完成当前底座；per-core run queue 和真实 SMP scheduler 已在 Stage 9 完成当前底座；用户态 ELF loader、COW fork、信号继续留给后续阶段。多核同步的第一层语义已在 Stage 6 落地。

### Stage 6: 原子操作、多核、内存模型 已完成当前底座

```text
LOCK 前缀教学模型，非法 LOCK 用法进入 DecodeError/logic_error
CMPXCHG/XADD/MFENCE 指令语义
x86 TSO store buffer、load forwarding、fence/drain、locked CAS/fetch-add
multi-core CoreId/CoreTopology
Machine/BootContext processor topology 校验
unit/integration/benchmark/docs
```

### Stage 7: timer、APIC、抢占、TLB shootdown 已完成当前底座

```text
local APIC + IOAPIC 教学模型
timer interrupt
preemptive context switch
sleep/wait queue
IPI
INVLPG/PCID/TLB shootdown
多核心 scheduler handoff
```

Stage 7 仍保持教学范围：当前是确定性 APIC/timer/IPI/shootdown 和 scheduler handoff 入口，不是假装已经有完整 SMP kernel。Stage 8 已经把 cache/pipeline/perf counter 接上，Stage 9 已经补齐 per-core scheduler 当前底座，Stage 10-15D 已经把用户进程、终端交互、文件系统和宿主机交互入口接入主线，下一步再逐步扩成窗口终端 adapter、exec/wait、pipe/redirect、page cache/mmap 和更复杂的设备模型。

### Stage 8: cache、pipeline、性能计数 已完成当前底座

```text
L1I/L1D set-associative cache
cache line / set / associativity geometry
LRU victim
write-back/write-through
dirty eviction
in-order pipeline retire/flush/stall
perf counters
Executor/MMU optional Stage8PerformanceModel hook
unit/integration/chaos/benchmark/docs
```

Stage 8 仍不假装已经有完整微架构：当前没有 MESI/MOESI、一致性探测、分支预测器、uop cache、乱序执行或真实 TSC。这些应在 SMP scheduler、cache coherence 和 SIMD/HPC 阶段逐步引入。

### Stage 9: 真实 SMP scheduler 与负载迁移 已完成当前底座

```text
per-core run queue
跨核心 wake/reschedule
负载迁移
抢占 tick 与 per-core scheduler 绑定
TLB shootdown ack 与核心本地执行闭环
unit/integration/chaos/benchmark/docs
```

Stage 9 仍保持教学范围：当前支持 queued READY thread migration 和确定性 rebalance，不假装已经有真实并发锁、NUMA、CFS/EEVDF、完整 CPU hotplug 或真正并行执行器。这些应在后续调度策略、同步原语和多核执行阶段继续推进。

### Stage 10: 用户态 loader、地址空间隔离、COW/futex

```text
用户态 loader
内核/用户地址布局
COW fork
futex/event 等等待语义
signal/async event 初版
```

### Stage 11: x86-64 syscall/trap 用户内核边界

```text
SyscallFrame ABI
负 errno 返回值
handle_user_syscall + SYSRET
MAP_ANON_PAGE / GETPID / FORK_COW
FUTEX_WAIT / FUTEX_WAKE_ONE / FUTEX_WAKE_ALL
用户 page fault: COW -> demand page -> kill
canonical SYSRET guard
```

### Stage 12: display terminal / console / TTY 交互地基

```text
TextDisplayBuffer
KeyboardInputQueue
TerminalDevice in Machine
BootContext terminal resource
Kernel console_write/console_read
TTY canonical line discipline
blocking read + terminal input wakeup
emulator stage12 smoke
```

### Stage 13: shell + fd/stdin/stdout/stderr

```text
ShellParser
builtin command registry
help/clear/echo/ps/mem/cpu/ticks/exit
FileDescriptor value object
stdin/stdout/stderr
read/write syscall 接 TTY
```

### Stage 14: TTY 驱动的交互式 shell loop

```text
ShellSession
stdout prompt
stdin blocking read
pending line buffer
多行输入逐条执行
exit/pending/io error step result
emulator stage14 smoke
```

### Stage 15: 文件系统、块设备、进程 API

```text
Stage 15A 已完成: block device + buffer cache
Stage 15B 已完成: SimpleFS + VFS inode/file object
Stage 15C 已完成: open/read/write/close/stat/readdir syscall + shell file commands
Stage 15D 已完成: host interactive terminal adapter + mnos_console
后续扩展: 可选窗口终端 adapter
exec/wait/process lifecycle
pipe/dup/redirect
page cache / mmap / demand file paging
block DMA / async I/O
```

### Stage 16: 高性能网络

```text
NIC descriptor ring
interrupt vs polling
zero-copy
高性能网络 benchmark
```

### Stage 17: HPC、SIMD、AI 推理/训练

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
builds/debug/mnos_console
```
