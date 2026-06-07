# MNOS 架构说明

MNOS 当前阶段的定位是：先构建一个可测试、可扩展的 x86-64 CPU 模拟核心，然后在 CPU core 之上继续发展 OS/kernel 相关模块。当前仓库还没有真正的 OS 代码，所以这次重构没有凭空创建空功能，而是先把 CPU、模拟器入口、测试、通用工具的边界搭好。

## 当前目录

```text
include/mnos/
  core/                 通用基础设施，例如 EnumMap 模板
  cpu/                  CPU core 的公开 API
    common/             CPU 基础类型、数据宽度
    register/           通用寄存器编号和寄存器组
    flags/              RFLAGS 状态位
    instruction/        指令 opcode、operand、instruction
    execution/          CPU 状态、程序容器、执行器、执行 trace

src/
  cpu/                  CPU core 的实现，目录结构镜像 include/mnos/cpu
  emulator/             宿主机上的模拟器入口程序

tests/
  cpu/                  CPU core 单元测试

docs/
  architecture/         项目结构和设计说明
  questions/            C++ 学习问题整理
```

未来真正添加 OS 代码时，建议按下面的边界扩展，而不是把 CPU 模拟器和 OS 逻辑混在一起：

```text
include/mnos/os/
  kernel/               kernel object、boot flow、panic/assert
  mm/                   physical/virtual memory、page table、allocator
  sched/                task/thread/process scheduler
  fs/                   file system abstraction
  syscall/              syscall ABI and dispatcher

src/os/
  kernel/
  mm/
  sched/
  fs/
  syscall/
```

核心依赖方向应该保持为：

```text
mnos_emulator -> mnos_os -> mnos_cpu
tests/*       -> 被测目标
```

CPU core 不应该反向依赖 OS。CPU 负责寄存器、指令、flags、内存访问接口和执行语义；OS 负责进程、内存管理、文件系统、系统调用、调度等更高层语义。这样后面既可以用同一个 CPU core 跑 OS 测试，也可以单独测试 CPU 指令行为。

## 当前 CPU 执行层

这一步已经把 CPU core 从“能描述简单指令”推进到“能执行一段指令流”。新增模块在：

```text
include/mnos/cpu/execution/
  cpu_state.hpp         CPU 可变状态：通用寄存器、RFLAGS、RIP、halted
  program.hpp           连续指令容器，RIP 作为指令下标
  trace.hpp             可选执行轨迹，记录 cycle、RIP 前后、opcode、停机状态
  executor.hpp          单核取指-译码-执行循环

src/cpu/execution/
  cpu_state.cpp
  program.cpp
  trace.cpp
  executor.cpp
```

当前执行器支持：

```text
MOV    写寄存器
ADD    写寄存器，并更新 ZF/SF/CF/OF
SUB    写寄存器，并更新 ZF/SF/CF/OF
CMP    只更新 ZF/SF/CF/OF，不写回寄存器
JMP    无条件跳转
JE     ZF=1 时跳转
JNE    ZF=0 时跳转
HALT   停机
```

这里先做单核执行循环，而不是直接上线程/进程，是因为线程和进程最终都要落到“某个 core 在某个时刻执行某个上下文”。没有 `CpuState + Executor`，后续 scheduler 只能停留在概念层；有了它，后面就可以把 thread/process 建模成“保存和恢复 CPU 上下文 + 调度队列 + 内存地址空间 + 同步对象”。

当前执行器的性能边界：

1. `Executor::step` 热路径使用 `switch (Opcode)`，没有虚函数分发。
2. `ExecutionTrace*` 是可选指针，不传 trace 时不产生 trace 存储开销。
3. `Program` 使用连续 `std::vector<Instruction>`，取指是下标访问，缓存局部性好。
4. flags 更新由局部 helper 集中处理，避免 ADD/SUB/CMP 到处重复写溢出/借位逻辑。

内存操作数现在会抛出明确错误，这是有意保留的边界：下一阶段应该先加入 `MemoryBus`，再让 `MOV [RBP + offset], RAX` 这种语义接入内存系统。这样 CPU executor 不会直接依赖 OS 的页表、物理页或虚拟地址空间。

## CMake 目标

当前 CMake 有三个主要目标：

```text
mnos_cpu        静态库，CPU 模拟核心
mnos_emulator   可执行程序，宿主机模拟器入口
mnos_cpu_tests  CPU core 测试程序
```

这样拆分的好处：

1. CPU core 可以被 emulator、OS、测试复用。
2. include path 使用 `target_include_directories` 绑定在目标上，避免全局污染。
3. warning、coverage、C++ 标准都通过目标作用域配置，不会意外影响无关目标。
4. 未来添加 `mnos_os` 时，只需要让 OS 目标链接 `mnos_cpu`，不用把所有文件塞进一个 executable。

## 当前代码中的现代 C++ 思想

### 1. 表驱动枚举映射

CPU 项目中会频繁出现“枚举值 -> 名称/bit index/大小”的映射，例如 `RegisterId -> "RAX"`、`FlagId -> bit index`、`DataSize -> bytes`。如果每个模块都手写 `static_cast`、数组访问、边界检查，就会重复且容易写错。

当前用 `include/mnos/core/enum_map.hpp` 提供 `EnumMap` 模板：

```cpp
constexpr auto TABLE = mnos::core::make_enum_map<MyEnum>(
    std::array<std::string_view, MY_ENUM_COUNT>{"A", "B"});
```

它把这些操作集中起来：

```cpp
table.contains(key);             // 是否是合法枚举
table.at(key, "invalid key");     // 合法则取值，非法则抛异常
table.value_or(key, fallback);    // 合法则取值，非法则给 fallback
```

这是一种“表驱动设计”：业务模块只声明事实表，通用模板负责重复机制。好处是错误更集中、测试更容易、代码更短。

### 2. Operand 用 `std::variant` 建模

操作数本质是一个“和类型”：

```text
Operand = None | Register | Immediate | Memory
```

旧写法是一个 `kind` 加很多字段：

```cpp
OperandKind kind;
RegisterId register_id;
SQWORD64 immediate_value;
RegisterId memory_base_register;
SQWORD64 memory_displacement;
DataSize memory_data_size;
```

这种写法会产生大量无效组合，例如 `kind == IMMEDIATE` 时 `memory_data_size` 仍然有一个值，但这个值没有意义。

现在用：

```cpp
using Storage = std::variant<std::monostate, RegisterPayload, ImmediatePayload, MemoryPayload>;
```

这让类型本身表达“不可能同时是寄存器和立即数”。访问 payload 时，如果类型不匹配就抛 `std::logic_error`，这比返回一个无意义默认值更清晰。

### 3. 静态工厂函数维护对象不变量

`Operand` 和 `Instruction` 都使用静态工厂函数：

```cpp
auto op = mnos::cpu::Operand::reg(mnos::cpu::RegisterId::RAX);
auto ins = mnos::cpu::Instruction::make_mov(mnos::cpu::Operand::reg(...), mnos::cpu::Operand::imm(...));
```

工厂函数的意义不是“看起来高级”，而是把创建规则集中在一个入口：

1. 创建寄存器操作数时校验寄存器 ID。
2. 创建内存操作数时校验 base register 和 data size。
3. 创建跳转指令时自动把第二操作数设为 `none`。

这属于轻量的 Factory Method 思想：对象内部表示可以变化，但外部通过稳定的命名构造语义对象。

### 4. 错误边界

当前规则：

1. “把枚举转成 index”不抛异常，只做普通 cast。
2. “访问表里的必需值”遇到非法枚举会抛 `std::out_of_range`。
3. “访问 Operand payload”但 kind 不匹配会抛 `std::logic_error`。
4. “名字映射”遇到非法枚举返回 `"<invalid>"`，因为调试/打印函数通常不应因为展示失败而中断流程。

这就是错误边界设计：哪些 API 是查询，哪些 API 是严格访问，哪些 API 是构造校验，要有一致语义。

### 5. 标准库风格容器接口

`Program` 和 `ExecutionTrace` 都提供了标准库风格接口：

```cpp
program.empty();
program.size();
program.begin();
program.end();
program.instructions(); // std::span<const Instruction>
```

这不是形式主义。它的好处是调用者能用熟悉的 C++ 容器语义理解复杂 CPU 概念：

1. `Program` 是一段连续 instruction stream。
2. `RIP` 当前阶段映射为指令下标。
3. `ExecutionTrace` 是执行事件的顺序日志。
4. `std::span` 返回只读视图，不复制 vector，也不暴露修改权限。

这类接口后面可以自然接入标准算法、调试 dump、测试断言和性能统计。

## 未来扩展建议

### Memory bus

后续可以添加：

```text
include/mnos/cpu/execution/
  memory_bus.hpp
  memory.hpp

src/cpu/execution/
  memory_bus.cpp
  memory.cpp
```

建议接口大致分层：

1. `RegisterBank` 管寄存器。
2. `Rflags` 管 flags。
3. `CpuState` 聚合寄存器、flags、RIP、halted。
4. `MemoryBus` 抽象内存读写，CPU 不直接知道 OS 的物理页或虚拟页。
5. `Executor` 根据 `Instruction` 修改 CPU state，并通过 `MemoryBus` 访问内存。

### OS/kernel

OS 层不要直接操作 `RegisterBank` 的内部数组，而应该通过 CPU core 的公开 API 或执行上下文访问状态。后续可以先做：

```text
include/mnos/os/kernel/kernel.hpp
include/mnos/os/mm/page.hpp
include/mnos/os/mm/address.hpp
```

推荐先设计几个明确边界：

1. physical address 和 virtual address 分开建类型，避免地址语义混淆。
2. page size、flag bits、权限位全部用命名常量或 `enum class`。
3. 页表遍历用显式循环/栈，不使用递归。
4. syscall ABI 放在 syscall 模块，不要散落在 CPU executor 里。
5. 线程/进程的上下文切换应该保存 `CpuState`，而不是让 scheduler 直接修改 executor 内部实现细节。

这样后面你学习 OS 时，CPU 指令语义、内存系统、OS 抽象会是互相连接但不互相污染的结构。
