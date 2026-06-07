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

src/
  cpu/                  CPU core 的实现
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
constexpr auto TABLE = mnos::make_enum_map<MyEnum>(
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
auto op = mnos::Operand::reg(mnos::RegisterId::RAX);
auto ins = mnos::Instruction::make_mov(mnos::Operand::reg(...), mnos::Operand::imm(...));
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

## 未来扩展建议

### CPU executor

后续可以添加：

```text
include/mnos/cpu/execution/
  cpu.hpp
  memory_bus.hpp
  executor.hpp

src/cpu/execution/
  cpu.cpp
  executor.cpp
```

建议接口大致分层：

1. `RegisterBank` 管寄存器。
2. `Rflags` 管 flags。
3. `MemoryBus` 抽象内存读写，CPU 不直接知道 OS 的物理页或虚拟页。
4. `Executor` 根据 `Instruction` 修改 CPU state。

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

这样后面你学习 OS 时，CPU 指令语义、内存系统、OS 抽象会是互相连接但不互相污染的结构。
