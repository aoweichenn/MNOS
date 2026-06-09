# OS Stage 0 学习说明

OS Stage 0 的目标是建立现代 x86-64 OS 必须依赖的硬件边界，而不是马上写完整进程和调度器。Stage 5 已在这个边界上补齐了第一版进程、地址空间、缺页处理、scheduler 和最小 syscall ABI；Stage 6 进一步补入 core topology、`LOCK` 原子指令和 x86 TSO 教学内存模型；Stage 7 加入 local APIC/IOAPIC、timer interrupt、抢占 tick、sleep/wait queue、IPI、PCID/INVLPG/TLB shootdown 和 scheduler handoff 入口；Stage 8 加入 cache、pipeline 和 perf counter 第一版性能硬件底座；Stage 9 加入 per-core run queue、SMP scheduler、跨核心 wake/reschedule、ready-thread migration 和 TLB shootdown 本地 apply 闭环；Stage 10 加入用户态地址布局、user program loader、COW fork、futex 和 event 等第一版用户进程运行语义；Stage 11 将这些能力接入 x86-64 syscall/trap 用户内核边界；Stage 12 加入文本终端硬件模型、kernel console 和 TTY 行规程，让系统开始具备交互入口；Stage 13 加入进程 stdio fd 表、READ/WRITE syscall 和 shell builtin，让终端从显示设备变成可执行命令入口；Stage 14 把 prompt、stdin blocking read、pending line buffer、scheduler wake 和 shell builtin 执行贯穿成可轮询交互 loop；Stage 15A 加入内存块设备和 buffer cache；Stage 15B 在块缓存上加入 SimpleFS、inode/dirent 和 VFS file object；Stage 15C 把 root VFS 接入 fd table、open/read/write/close/stat/readdir syscall 和 shell 文件命令；Stage 15D 加入 host 侧交互终端 adapter 和 `mnos_console`；Stage 15E-A 拆出可插拔 host terminal backend、host 输入事件模型和 renderer 边界；Stage 15E-B 加入 raw-key console 输入模式，让交互终端不再依赖宿主行缓冲。

```text
Machine       模拟机器入口，持有物理内存、MemoryBus、core topology、TerminalDevice
BootContext   kernel 启动资源视图，暴露 terminal 设备
Kernel        启动状态机 + Stage7 APIC/timer/sleep/shootdown + Stage9 SMP + Stage10 user/COW/futex + Stage11 syscall/trap + Stage12 console/TTY + Stage13 fd/syscall bridge + Stage14 shell loop 编排
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

Stage 9 已经完成第一版 SMP scheduler 底座：

```text
SmpScheduler          每核心 run queue/current，保留旧 RoundRobinScheduler 教学路径
create_thread_on_core 按目标 core 创建并入队线程
SMP timer             只抢占目标 core 的 current，记录 per-core tick/preemption
cross-core wake       wake 到目标 core run queue，并发送 reschedule IPI
ready migration       迁移 queued READY thread，running thread 不做伪热迁移
rebalance_once        从 ready 队列最重的 core 向最轻 core 移动一个 runnable thread
TLB local apply       目标 core take/apply/ack pending shootdown request
Benchmark             SMP scheduler cycle 与 Kernel SMP timer 已进入 benchmark smoke
```

Stage 10 已经完成第一版用户进程与等待语义：

```text
AddressLayout       明确 user low/text/heap/stack/kernel high 的地址边界和 user stack bottom 计算
UserProgram/Segment 描述教学级用户镜像，区分 text/data/bss 权限和 entry 可执行性
UserLoader          分配并清零物理页，复制 segment bytes，映射用户栈，初始化 ring3 RIP/RSP/PCID
AddressSpace        支持按 PCID 激活、查询 page translation、更新 4KiB PTE 权限
COW fork            可共享 writable page 为只读 COW，支持嵌套 fork、写时复制、最后引用恢复权限
Futex/Event         进程作用域 futex word key、非拥有等待队列、signal/wake 后交给 scheduler 恢复 READY
Kernel facade       create_user_process、fork_process_cow、handle_cow_write_fault、wait/wake futex
```

Stage 11 已经完成第一版用户态 syscall/trap 边界：

```text
Syscall ABI       RAX 传 number，RDI/RSI/RDX/R10/R8/R9 传 6 个参数，RAX 返回值或负 errno
Syscall catalog   YIELD/EXIT/GETPID/MAP_ANON_PAGE/FORK_COW/FUTEX_WAIT/FUTEX_WAKE_ONE/FUTEX_WAKE_ALL
Trap boundary     Kernel handle_user_syscall 校验 ring3 syscall trap，snapshot trapframe，完成 SYSRET
User page fault   ring3 #PF 先走 COW write fault，再走 demand page，非法用户地址 kill thread
MAP_ANON_PAGE     教学级匿名 4KiB 零页映射，校验 user/page-aligned/already-mapped/out-of-memory
Futex syscall     wait 会读取用户 futex word 并按 expected 值决定 EAGAIN 或 BLOCKED，wake 返回唤醒数量
PCID helper       ProcessId -> x86-64 PCID 规则抽到 process_context，UserLoader/Kernel 共用
SYSRET guard      CPU TrapController 校验返回 RIP/RSP canonical，避免非法用户返回状态静默扩散
```

Stage 12 已经完成第一版终端交互底座：

```text
TextDisplayBuffer   可配置文本显存，保存 char/attribute、cursor、clear、scroll、render
KeyboardInputQueue  确定性 key event FIFO，支持容量限制和 drain
TerminalDevice      display + keyboard 的机器硬件 facade，由 Machine 持有并通过 BootContext 暴露
Console             kernel console write、TTY canonical line input、echo、backspace、enter 成行
TTY wait path       read 无完整行时进入 WaitQueue，终端输入完成一行后返回可唤醒线程
Kernel facade       console_write、console_read、submit_terminal_input，负责与 scheduler wake/block 集成
Emulator smoke      启动时写入模拟终端，并打印 stage12=ready
```

Stage 13 已经完成第一版 shell/stdio 底座：

```text
FileDescriptor        stdin/stdout/stderr value object，错误 fd 明确返回 EBADF
FileDescriptorTable   Process 默认持有 TTY stdio，fork COW 复制 fd table；Stage15C 后共享 open-file-description offset
READ/WRITE syscall    RAX number、RDI fd、RSI user buffer、RDX count，返回 byte count 或负 errno
User buffer checks     先校验 fd，再校验长度/user range/MMU access，避免 bad fd 被坏指针掩盖
TTY stdio bridge       stdin read 可 BLOCKED 并进入 scheduler block，stdout/stderr write 输出到 console
ShellParser            支持空白分隔、单双引号、反斜杠转义和未闭合引号错误
Builtin registry       help/clear/echo/ps/mem/cpu/ticks/exit，静态目录 + 函数指针分发
Shell session          parser + registry + Kernel console context，emulator smoke 打印 stage13=ready
```

Stage 14 已经完成第一版交互式 shell loop：

```text
ShellSession          Shell 外层 session，绑定 Kernel/Process/ThreadContext，负责 poll 一次交互进展
Prompt                poll 读取前通过 stdout fd 写入 mnos>，为未来重定向保留 fd 边界
Blocking stdin        stdin 无完整 TTY 行时返回 BLOCKED，当前 shell 线程进入 scheduler block
Wake path             submit_terminal_input 完成一行后唤醒等待线程，下一次 poll 执行命令
Pending input         支持半行、多行、长行分片和 CRLF，固定 read buffer 不截断命令
Step result           BLOCKED/PENDING_INPUT/COMMAND/EXITED/IO_ERROR 显式建模
Emulator smoke        先 poll 到 BLOCKED，再输入 echo shell ready，最终打印 stage14=ready
```

Stage 15A 已经完成第一版块设备和 buffer cache 地基：

```text
BlockAddress          显式块号值对象，避免和 byte offset/page number 混用
BlockDeviceGeometry   block size/block count/capacity 校验，拒绝零大小和 uint64 overflow
MemoryBlockDevice     vector-backed 教学块设备，支持整块和连续多块读写、clear、contains
BufferCache           固定容量缓存，哈希命中索引 + LRU victim，内部连续 byte storage
Write-back path       write 命中/缺失只更新缓存并标脏，flush/dirty eviction 才写回设备
Stats                 hit/miss、device read、write call、device writeback、dirty eviction 计数
Benchmark             MemoryBlockDevice read/write 与 BufferCache read-hit 已进入 benchmark smoke
```

Stage 15B 已经完成第一版 SimpleFS 和 VFS 文件对象：

```text
SimpleFS format/mount  superblock、inode table、data bitmap、data block 区的真实块格式
Inode/dirent           固定 inode record、direct block、固定目录项，支持 file/directory kind
File I/O               跨块 read/write、overwrite、append、direct-block 容量检查和 no-sparse 约束
Directory I/O          root/nested directory、lookup、create_file、create_directory、read_directory
VFS facade             absolute path 解析、lookup/create/open_file/read_directory，不引入 syscall
VfsFile                持有 inode + open mode + offset，支持 read/write/seek 和 mode 校验
Benchmark              SimpleFS file read/write 已进入 benchmark smoke
```

Stage 15C 已经完成第一版文件 syscall 和 shell 文件交互：

```text
Kernel root VFS         boot 时创建内存块设备、buffer cache、SimpleFS 和 VFS root mount
OpenFileDescription    fd entry 持有共享 open-file-description，fork 后共享 VfsFile offset
VFS fd bridge          Kernel::open_file/close_fd/read_fd/write_fd 接入 VFS_FILE descriptor kind
File syscalls          OPEN/CLOSE/STAT/READDIR，READ/WRITE 复用 fd bridge 访问真实文件
User ABI               path 使用 user address + length，stat/dirent 固定小端 record，负 errno 返回
Shell file commands    ls/cat/touch/write/stat 通过 Kernel::vfs() 访问 root VFS
Benchmark              Kernel VFS open/close fd 已进入 benchmark smoke
```

Stage 15D / 15E-A / 15E-B 已经完成第一版宿主机交互终端和后端边界：

```text
TerminalRunner        shell drive facade，负责 boot、ShellSession::poll 和 run result 汇总
HostTerminalBackend   stdio/window/raw terminal 的 host adapter 接口，不让 GUI 依赖进入 kernel
HostInputEvent        host 输入事件模型，区分 text、special key、input closed、host I/O error
StreamTerminalBackend 当前 stdio backend，读取宿主机 stdin，规范化 CRLF，提交到 Kernel::submit_terminal_input
Raw input mode        逐字节解析 Enter、Backspace/Delete、Tab、Ctrl-C、Ctrl-D 和 ANSI CSI 方向键
mnos_console input    交互 TTY 默认 raw，pipe 默认 line；--raw/--line 可显式覆盖
Shell drive           复用 ShellSession::poll 驱动 prompt、blocking read、命令执行和 exit
Stream renderer       消费 TerminalDevice 增量输出流，默认避免历史屏幕重放
Screen renderer       保留 TextDisplayBuffer 快照输出，支持显式 ANSI screen 与 plain screen 调试模式
mnos_console          真实可运行入口：builds/debug/mnos_console
Test harness          覆盖输入事件、raw key、渲染 drain、clear/backspace、EOF、I/O error 和 replay 回归
```

## 下一步

合理顺序：

```text
1. 可选窗口终端 adapter，复用 host terminal 边界，不让 GUI 依赖进入 kernel
2. exec/wait/process lifecycle，把 user loader、syscall、VFS fd 和 shell 连成可运行用户程序闭环
3. pipe/dup/redirect，把 shell 变成可组合的交互环境
4. page cache / mmap / demand file paging，把文件系统和虚拟内存合流
5. network device + packet ring + high-performance network path
6. cache coherence / branch predictor / uop cache / SIMD
7. HPC/SIMD/AI 推理训练路线
```

这样学习者能从真实 x86-64 的 CPU 状态走到现代 OS，而不是只看抽象 API。
