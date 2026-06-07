# C++ 与 MNOS 学习问题合集

这份文档把 C++ 语言规则和当前 x86-64 Stage 0 模型连起来。示例使用 `RIP/RFLAGS/Operand/MOV/HLT` 等当前主线术语。

## 1. 编译期常量和运行时常量

编译期常量能在编译阶段确定：

```cpp
constexpr std::size_t REGISTER_COUNT = 16;
std::array<cpu::UQWORD64, REGISTER_COUNT> registers{};
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

Stage 0 用 `Operand` 建模：

```text
none
register
immediate
memory(base register + displacement + data size)
```

`Operand` 使用 `std::variant`，是为了把合法 payload 绑定到类型上，而不是用一堆松散字段。

## 5. RIP 和 Program

真实 x86-64 的 `RIP` 是字节地址，指令长度可变。Stage 0 还没有机器码 decode，所以 `Program` 暂时是对象指令数组，`RIP` 表示当前对象指令槽位。

后续 Stage 1 会把它升级为：

```text
RIP -> MemoryBus 取 byte -> variable-length decoder -> Instruction
```

这一步不能跳过，否则很难测试 RFLAGS、内存和跳转语义。

## 6. RFLAGS

x86-64 普通整数指令会更新隐式状态位：

```text
ADD/SUB/CMP -> CF/ZF/SF/OF
JE/JNE      -> 读取 ZF
```

当前 `Rflags` 先建模：

```text
CF Carry
ZF Zero
SF Sign
OF Overflow
```

这对学习高性能代码很重要，因为很多分支、比较、条件移动、原子操作都会和 flags 相关。

## 7. DataSize

x86-64 常用数据宽度：

```text
BYTE   8 bit
WORD   16 bit
DWORD  32 bit
QWORD  64 bit
```

当前内存读写按小端序实现。后续加入真实 decode 后，ModRM/SIB 和 opcode 会决定访问宽度。

## 8. 为什么热路径不用复杂模式？

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

## 9. 下一阶段学习重点

```text
x86-64 instruction byte encoding
REX prefix
ModRM/SIB
RIP-relative addressing
CALL/RET/PUSH/POP
exception/syscall/interrupt
4-level paging
TLB/cache
LOCK/atomic and x86 TSO
SSE/AVX performance path
```
