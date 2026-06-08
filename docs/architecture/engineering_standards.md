# MNOS 工程标准

MNOS 后续会面向现代 OS、现代 CPU、HPC、高性能网络、分布式系统和 AI 推理/训练学习路径。底座必须按现代 C++20 和现代软件工程要求建设，不能为了短期演示牺牲边界和性能。

## ISA 主线

1. 主线采用 x86-64 ISA。
2. 不再新增 RV 指令、RV ABI 寄存器名或 RV privileged 术语污染主线。
3. 后续如需研究 RV，可以作为稳定机器模型之上的独立 backend，不污染 x86-64 主线。

## 模块边界

```text
CPU core       ISA、寄存器、RIP/RFLAGS、执行、内存访问
Memory system  PhysicalMemory、MemoryBus、MMU、TLB、cache
Platform       Machine、device bus、timer、interrupt controller、设备 adapter
OS kernel      boot、trap、scheduler、process、syscall、fs、network
```

CPU core 不反向依赖 OS。设备、MMU、cache、scheduler 等模块通过明确接口连接，不通过全局可变状态连接。

## C++20 代码规范

1. 类成员访问使用 `this->`。
2. 公共头文件保持薄，只暴露稳定 API；复杂实现放到 `.cpp`。
3. 常量使用有领域含义的全大写名称。
4. 类型和函数命名要让命名空间承载模块，例如 `cpu::Qword`、`cpu::Address64`、`os::sched::ThreadState`；不要在命名空间内重复写冗长模块前缀。
5. 跨模块可见的常量、CMake target、测试 fixture 名称必须带模块语义，避免离开命名空间后失去上下文。
6. 不保留死代码、旧 ISA 分支、不可达 fallback。
7. 优先使用 `enum class`、`std::span`、`std::string_view`、RAII、强类型 value object、表驱动映射。
8. 避免无意义拷贝，热路径参数按引用或轻量值传递。
9. 避免运行时递归，树/图/队列类处理使用显式 stack、queue、worklist。

## 设计模式使用原则

设计模式必须解决真实问题：降低耦合、明确生命周期、隔离变化点、减少重复、保护不变量。不要为了显得“高级”而引入模式。

适合 MNOS 的模式：

```text
Facade        Machine、MemoryBus、KernelServices
Adapter       UART/NIC/BlockDevice/MMIO host adapter
Strategy      x86 decoder、scheduler policy、cache replacement policy
Builder       MachineConfig、BootConfig、CoreTopology
Factory       Instruction 构造、Device 创建、TrapFrame 构造
Observer      Trace/Event/PerfCounter
Value Object  Address、ThreadId、PageNumber、MSR id
```

性能热路径要谨慎使用虚函数和堆分配。`Executor`、TLB 查询、cache 查询、调度队列、网络 ring、AI/HPC kernel benchmark 应优先考虑连续数据、直接分派、表驱动和可测量的低开销接口。

无状态静态 helper 类可以用于封装清晰的规范逻辑，例如 flags 更新、操作数校验、枚举表查询。只要它不引入虚函数分发、堆分配或可测量热路径成本，就不要为了形式上的“函数化”把职责拆散。

## 测试和性能基线

每个阶段都至少保持：

```text
cmake --build builds/debug --parallel
ctest --test-dir builds/debug --output-on-failure
cmake --build builds/debug --target mnos_benchmark_smoke
builds/debug/mnos_emulator
builds/debug/mnos_console
```

新增模块应配套：

```text
unit        单模块正常/错误路径
integration 跨模块协议和状态流
chaos       固定 seed 长流程
fuzz        固定 seed 边界组合
benchmark   热路径性能基线
docs        架构说明和学习路线
```

新增或触碰代码覆盖率目标为 90% 以上。覆盖率只能来自真实行为、边界和风险路径测试，不能靠空断言、死路径、削弱工具或低价值用例凑数字。

性能退化不能靠感觉判断。后续 hot path 变更要尽量配合 benchmark 或 trace 说明。
