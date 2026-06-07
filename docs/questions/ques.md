# C++ 问题合集

这份文档按“初学者能建立底层模型”的方式回答几个常见 C++ 问题。重点不是背结论，而是理解：语言规则想保证什么，编译器能优化什么，运行时到底保存了什么。

## 1. 什么是编译器常量，什么是运行时常量？

更准确的说法是：

```text
编译期常量：值能在编译期确定，并且可以参与 constant expression 的值。
运行时常量：对象初始化以后不能被修改，但它的值要到运行时才知道。
```

### 编译期常量

编译期常量可以在编译器生成机器码之前就确定。

典型例子：

```cpp
constexpr int CPU_REGISTER_COUNT = 16;

std::array<int, CPU_REGISTER_COUNT> registers{};
```

这里 `CPU_REGISTER_COUNT` 必须在编译期知道，因为 `std::array<int, N>` 的 `N` 是模板参数。模板参数属于类型的一部分，编译器必须提前知道。

再比如：

```cpp
constexpr int PAGE_BITS = 12;
constexpr int PAGE_SIZE = 1 << PAGE_BITS;

static_assert(PAGE_SIZE == 4096);
```

`static_assert` 在编译期检查，所以 `PAGE_SIZE` 必须能在编译期求值。

### 运行时常量

运行时常量通常用 `const` 表示。它初始化之后不能再改，但初始化值可能来自运行时。

```cpp
int read_from_user();

const int value = read_from_user();
```

`value` 是常量，因为后续不能写：

```cpp
value = 42; // 编译错误
```

但它不是编译期常量，因为 `read_from_user()` 的结果只有程序运行后才知道。

所以这段代码不合法：

```cpp
int read_from_user();

const int count = read_from_user();
std::array<int, count> data{}; // 错误：count 不是编译期常量
```

### `const` 不等于编译期常量

初学者最容易误解的是：

```text
const 表示“不能通过这个名字修改对象”。
constexpr 表示“这个值必须能在编译期求出”。
```

例子：

```cpp
constexpr int A = 10;       // 编译期常量
const int B = 20;           // 通常也可作为编译期常量，因为初始化表达式是常量表达式
const int C = get_value();  // 运行时常量
```

`B` 能不能当编译期常量，关键不是 `const` 本身，而是它的初始化表达式是否能在编译期求值。

### 常见关键字

`constexpr`：

```cpp
constexpr int add(int a, int b)
{
    return a + b;
}

constexpr int result = add(1, 2);
```

`constexpr` 函数表示：如果传入的是编译期常量，它可以在编译期执行；如果传入的是运行时值，它也可以在运行时执行。

```cpp
int x = read_from_user();
int y = add(x, 2); // 运行时执行
```

`consteval`：

```cpp
consteval int page_size_from_bits(int bits)
{
    return 1 << bits;
}

constexpr int PAGE_SIZE = page_size_from_bits(12);
```

`consteval` 函数必须在编译期执行，不能退到运行时。

`constinit`：

```cpp
constinit int GLOBAL_COUNTER = 0;
```

`constinit` 保证静态存储期对象在静态初始化阶段完成初始化，避免某些“静态初始化顺序”问题。但它不表示对象不可修改，也不表示这个对象能当 constant expression 使用。

### 编译器优化不等于语言保证

看这段：

```cpp
int x = 2 + 3;
```

编译器几乎一定会把它优化成 `5`。但这是优化，不是你写的变量一定就是语言意义上的编译期常量。

语言层面的编译期常量能用于：

```cpp
static_assert(...)
std::array<T, N>
template <int N>
case N:
```

优化层面的常量折叠只是编译器生成更好机器码的行为，不能改变 C++ 语法规则。

## 2. `std::string_view` 的原理是什么？什么时候能构造字符串？具体含义是什么？

`std::string_view` 是“字符串视图”，不是字符串本体。

可以先把它理解成：

```cpp
struct string_view_like
{
    const char* data;
    std::size_t size;
};
```

真实的 `std::string_view` 还带有字符 traits 等标准库细节，但核心思想就是：它只保存“指向哪里”和“长度是多少”。

### `std::string_view` 不拥有内存

例子：

```cpp
std::string name = "MNOS";
std::string_view view = name;
```

`view` 没有复制 `"MNOS"`，它只是看着 `name` 内部那段字符。

这意味着：

```cpp
name = "CPU";
```

之后 `view` 可能失效，也可能看到旧内存或新内存，具体取决于 `std::string` 是否重新分配了缓冲区。你不能把 `string_view` 当成拥有数据的字符串。

### 最安全的来源：字符串字面量

```cpp
std::string_view opcode = "HALT";
```

字符串字面量有静态存储期，程序结束前都存在。所以这种 `string_view` 很安全。

这也是当前项目里很多名字映射返回 `std::string_view` 的原因：

```cpp
std::string_view opcode_to_assembly_name(Opcode opcode) noexcept;
```

返回的是 `"MOV"`、`"ADD"`、`"HALT"` 这些静态字符串的视图，不需要动态分配，也不会悬垂。

### 危险例子：临时 `std::string`

```cpp
std::string_view bad = std::string{"MNOS"};
```

这非常危险。`std::string{"MNOS"}` 是临时对象，语句结束就销毁。`bad` 还保存着指向它内部字符的指针，于是变成悬垂视图。

正确写法：

```cpp
std::string name = "MNOS";
std::string_view good = name;
```

但也要保证 `name` 的生命周期长于 `good` 的使用期。

### `string_view` 不保证以 `'\0'` 结尾

C 风格字符串依赖结尾的 `'\0'`：

```cpp
const char* c = "MNOS";
```

但 `std::string_view` 有自己的长度：

```cpp
std::string text = "MNOS-CPU";
std::string_view part{text.data(), 4}; // 只看 MNOS
```

`part.data()` 指向的内存后面还有 `-CPU`，并不等于一个独立的 C 字符串。

所以不要随便这样传：

```cpp
printf("%s", part.data()); // 可能打印超过 view 的范围
```

如果要传给需要 `'\0'` 结尾的 C API，先构造拥有内存的 `std::string`：

```cpp
std::string owned{part};
printf("%s", owned.c_str());
```

### 什么时候能从 `string_view` 构造 `std::string`？

当你需要“拥有一份数据”时，就从 `string_view` 构造 `std::string`：

```cpp
std::string_view view = "RAX";
std::string owned{view};
```

这会复制字符。复制之后：

```cpp
owned[0] = 'X';
```

不会影响原来的字面量或原始字符串。

简单说：

```text
只读、不拥有、不延长生命周期：std::string_view
需要保存、修改、传给 C API、跨越原对象生命周期：std::string
```

### 函数参数什么时候用 `string_view`？

如果函数只读字符串，不保存它，`std::string_view` 很合适：

```cpp
bool starts_with_mnos(std::string_view text)
{
    return text.starts_with("MNOS");
}
```

调用时可以传：

```cpp
starts_with_mnos("MNOS");
starts_with_mnos(std::string{"MNOS"});
starts_with_mnos(std::string_view{"MNOS"});
```

但如果函数要保存参数，不能直接保存 `string_view`，除非你明确知道外部字符串一定活得更久。

## 3. `std::string` 为什么不是简单的编译期常量？

先区分两个概念：

```text
字符串字面量 "MNOS"：编译器生成的静态字符数组。
std::string：一个拥有内存、管理生命周期、可以修改内容的对象。
```

字符串字面量更接近：

```cpp
static const char TEXT[] = {'M', 'N', 'O', 'S', '\0'};
```

而 `std::string` 通常类似：

```cpp
class string_like
{
    char* data;
    std::size_t size;
    std::size_t capacity;
};
```

真实实现更复杂，还可能有 small string optimization。

### `std::string` 有所有权

```cpp
std::string s = "MNOS";
```

`s` 要负责：

1. 保存字符。
2. 记录长度。
3. 必要时申请堆内存。
4. 析构时释放内存。
5. 修改内容时管理容量。

这些都属于运行时对象管理。

### C++20 以后要更精确地说

现代 C++ 中，`std::string` 的很多操作可以参与常量求值。也就是说，在某些 C++20 及之后的环境里，你可以在 `constexpr` 函数内部临时使用 `std::string` 做计算。

但是它仍然不是像 `int`、枚举、指针、`std::string_view` 那样简单稳定的“编译期常量类型”。

原因包括：

1. 它可能需要动态分配。
2. 动态分配得到的内存不能从一次常量求值里“泄漏”到运行期对象中。
3. 它不是结构化类型，不能作为普通非类型模板参数。
4. 它的对象表示依赖标准库实现，例如是否使用 small string optimization。

所以学习时可以这样理解：

```text
std::string 可以在现代 C++ 的某些 constexpr 场景里参与编译期计算；
但它不是最适合作为头文件编译期字符串常量的工具。
```

头文件里的固定字符串常量通常优先用：

```cpp
inline constexpr std::string_view CPU_NAME = "MNOS x86-64 CPU";
```

或者：

```cpp
inline constexpr char CPU_NAME[] = "MNOS x86-64 CPU";
```

不要优先写：

```cpp
inline const std::string CPU_NAME = "MNOS x86-64 CPU";
```

因为这会引入更重的对象构造、析构和 ABI 细节。

### 为什么当前项目返回 `string_view`？

例如：

```cpp
std::string_view opcode_to_assembly_name(Opcode opcode) noexcept;
```

它返回的是静态表中的字面量视图。这样：

1. 不分配内存。
2. 不复制字符串。
3. 生命周期稳定。
4. 调用方如果要拥有字符串，可以自己构造 `std::string`。

这就是 `std::string_view` 的典型用法。

## 4. `inline` 到底是什么？`constexpr` 不能表示 `inline` 吗？

`inline` 最初有“建议编译器内联函数体”的含义，但现代 C++ 里更重要的意义是 ODR 规则。

ODR 是 One Definition Rule，也就是“一个实体在整个程序里应该如何定义”的规则。

### 函数放在头文件里为什么常常要 `inline`？

如果你在头文件里写：

```cpp
int add(int a, int b)
{
    return a + b;
}
```

然后两个 `.cpp` 都 include 这个头文件，链接时会看到两个 `add` 定义，可能产生 multiple definition 错误。

改成：

```cpp
inline int add(int a, int b)
{
    return a + b;
}
```

意思是：这个函数可以在多个翻译单元里有相同定义，链接器最终把它们当作同一个实体处理。

### `constexpr` 函数是否隐含 `inline`？

是的，`constexpr` 函数隐含 `inline`。

```cpp
constexpr int add(int a, int b)
{
    return a + b;
}
```

这个函数可以放在头文件里，因为它隐含 inline。

但这不表示 `constexpr` 和 `inline` 是同一个概念。

```text
constexpr：这个函数/变量和编译期求值有关。
inline：这个实体允许在多个翻译单元中有同一定义，解决 ODR 问题。
```

### 变量的情况更容易混淆

头文件里如果写：

```cpp
constexpr int PAGE_SIZE = 4096;
```

namespace scope 的 `constexpr` 变量默认是 `const`，通常具有 internal linkage。每个 `.cpp` 可能各有一份自己的 `PAGE_SIZE`。

如果你想表达“整个程序共享一个 header-defined 变量实体”，现代 C++ 推荐：

```cpp
inline constexpr int PAGE_SIZE = 4096;
```

这也是当前项目常量使用的形式：

```cpp
inline constexpr std::size_t DATA_SIZE_QWORD_BITS = 64;
```

含义是：

1. `constexpr`：值能在编译期使用。
2. `inline`：可以定义在头文件里，被多个 `.cpp` include。
3. `const` 属性：不能修改。

### `inline` 不保证机器码一定内联

这很重要：

```cpp
inline int add(int a, int b)
{
    return a + b;
}
```

编译器可以真的把函数体展开，也可以不展开。

真正控制优化的是编译器优化器，不是 `inline` 关键字本身。现代 C++ 中 `inline` 更应该被理解成链接规则，而不是性能按钮。

## 5. `static_cast<>` 的原理是什么？会不会有性能损耗？只是编译期处理的吗？

`static_cast<T>(value)` 是一种显式类型转换。它比 C 风格强转更清楚，因为它告诉读者：这是一个静态类型系统允许的转换。

例子：

```cpp
double x = 3.14;
int y = static_cast<int>(x);
```

这表示把 `double` 转成 `int`。

### `static_cast` 本身不是函数

这不是函数调用：

```cpp
static_cast<int>(x)
```

它是 C++ 语法，编译器在语义分析阶段检查它是否合法，然后生成相应代码。

### 会不会有性能损耗？

要看转换本身。

#### 1. 编译期能确定的转换，通常没有运行时成本

```cpp
constexpr int x = static_cast<int>(mnos::cpu::DataSize::QWORD);
```

这种枚举转整数，如果值在编译期确定，编译器直接知道结果。

#### 2. 数值类型转换可能生成机器指令

```cpp
double d = read_double();
int i = static_cast<int>(d);
```

`double -> int` 需要运行时转换，CPU 会执行对应指令。这不是 `static_cast` 额外收费，而是类型转换本身需要工作。

#### 3. 指针上行转换通常很轻

```cpp
Derived* d = get_derived();
Base* b = static_cast<Base*>(d);
```

单继承时可能就是同一个地址。多继承时，指针可能需要加一个偏移量，因为 `Base` 子对象不一定在 `Derived` 对象起始位置。

#### 4. 指针下行转换没有运行时检查

```cpp
Base* b = get_base();
Derived* d = static_cast<Derived*>(b);
```

这要求你自己保证 `b` 实际指向 `Derived` 对象。否则行为未定义。

如果你需要运行时检查，应该用：

```cpp
Derived* d = dynamic_cast<Derived*>(b);
```

但 `dynamic_cast` 需要多态类型，并且可能有 RTTI 成本。

### 为什么不要用 C 风格强转？

C 风格强转：

```cpp
int x = (int)value;
```

它可能在背后做 `static_cast`、`const_cast`、`reinterpret_cast` 的某种组合。读者很难知道你真正想做什么。

现代 C++ 推荐：

```cpp
static_cast<int>(value);          // 普通静态转换
const_cast<T>(value);             // 去掉 const/volatile
reinterpret_cast<T>(value);       // 重新解释二进制表示，非常危险
dynamic_cast<T>(value);           // 多态类型运行时检查
```

写清楚以后，代码审查和学习都更容易。

### 当前项目里的例子

```cpp
template <EnumKey Enum>
[[nodiscard]] constexpr std::size_t enum_to_index(const Enum value) noexcept
{
    return static_cast<std::size_t>(value);
}
```

这里 `static_cast` 的意图非常明确：把强类型枚举变成数组索引。`enum class` 不会隐式转整数，所以必须显式写出来。

这也是现代 C++ 的思路：

1. 用 `enum class` 防止乱转。
2. 在少数明确边界用 `static_cast`。
3. 把转换集中成一个函数，避免到处散落。

## 6. `static` 到底是什么？怎么理解？

`static` 是 C++ 里最容易混乱的关键字之一，因为它在不同位置含义不同。

可以先记住一句话：

```text
static 不是一个单一概念；它根据出现的位置，分别影响 linkage、storage duration 或 class 成员归属。
```

### 1. namespace/file scope 的 `static`：内部链接

在 `.cpp` 顶层写：

```cpp
static int helper_value = 42;

static void helper()
{
}
```

意思是：这些名字只在当前翻译单元可见，别的 `.cpp` 链接不到它们。

现代 C++ 更推荐匿名 namespace：

```cpp
namespace
{
int helper_value = 42;

void helper()
{
}
}
```

当前项目里的 `.cpp` 文件就使用匿名 namespace 放内部常量和 helper 函数。

### 2. 局部变量的 `static`：静态存储期，只初始化一次

```cpp
int next_id()
{
    static int id = 0;
    ++id;
    return id;
}
```

普通局部变量每次进入函数都会重新创建；`static` 局部变量只创建一次，程序结束时销毁。

执行过程：

```text
第一次调用 next_id：初始化 id = 0，然后 ++id，返回 1
第二次调用 next_id：沿用上次的 id = 1，然后 ++id，返回 2
第三次调用 next_id：沿用上次的 id = 2，然后 ++id，返回 3
```

C++11 起，局部静态变量初始化是线程安全的。编译器通常会生成一个 guard 变量，确保多线程同时第一次进入函数时只初始化一次。

### 3. class 里的 static data member：属于类，不属于某个对象

```cpp
class Cpu
{
public:
    inline static constexpr std::size_t REGISTER_COUNT = 16;
};
```

`REGISTER_COUNT` 不属于某个 `Cpu` 对象，而属于 `Cpu` 这个类。

访问方式：

```cpp
std::size_t count = Cpu::REGISTER_COUNT;
```

如果是非 `inline` 的静态数据成员，老式 C++ 还需要在 `.cpp` 里提供一次定义。C++17 以后常量更推荐：

```cpp
inline static constexpr std::size_t REGISTER_COUNT = 16;
```

这样可以直接放在头文件里。

### 4. static member function：没有 `this`

```cpp
class Cpu
{
public:
    static bool is_valid_register(int id)
    {
        return id >= 0 && id < 16;
    }
};
```

静态成员函数不绑定某个对象，所以里面没有 `this`。它不能直接访问普通成员变量：

```cpp
class Cpu
{
private:
    int rip = 0;

public:
    static int bad()
    {
        return rip; // 错误：static 函数没有 this
    }
};
```

它适合表达“不需要对象状态的类相关工具函数”。

不过在现代 C++ 中，如果函数不需要访问 private 成员，很多时候自由函数更清晰：

```cpp
bool is_register_id_valid(RegisterId id) noexcept;
```

当前项目就大量使用 namespace 下的自由函数，因为它们不需要绑定对象。

### 5. static storage duration

有些对象拥有静态存储期，意思是它们从程序开始附近存在到程序结束附近。

包括：

1. 全局变量。
2. namespace scope 变量。
3. `static` 局部变量。
4. class static data member。

这和“名字能不能被其他 `.cpp` 看到”不是同一个问题。

例如：

```cpp
int global_value = 1;
```

它有静态存储期，也有外部链接。

```cpp
static int file_value = 2;
```

它有静态存储期，但只有内部链接。

```cpp
void f()
{
    static int local_value = 3;
}
```

它有静态存储期，但名字只在函数内部可见。

### 6. 初学者怎么判断 `static` 的含义？

看它出现在哪里：

```text
.cpp 顶层 static 变量/函数：限制链接可见性，只在当前文件可见。
函数内部 static 变量：静态存储期，只初始化一次。
class 内 static 成员变量：属于类，不属于对象。
class 内 static 成员函数：没有 this，不依赖对象。
```

不要把这些混成一句“static 就是静态变量”。这会误导你。

## 7. CPU 模拟器为什么先做单核执行循环？

因为 OS 里的线程、进程、锁、信号量、多核调度，最后都会落到一个更底层的问题：

```text
某个 CPU core 现在正在执行哪条指令？
这条指令读写了哪些寄存器、flags、内存？
执行完以后 RIP 指向哪里？
什么时候停机、陷入内核、切换上下文？
```

如果没有这个底层执行循环，线程/进程只能是“数据结构模拟”，不能真正把“硬件如何推动 OS 行为”串起来。

### 最小 CPU 执行循环是什么？

当前项目里的核心模型可以简化成：

```cpp
while (!state.is_halted())
{
    const auto rip = state.rip();
    const Instruction& instruction = program.instruction_at(rip);
    executor.execute(instruction, state);
}
```

真实代码没有把 `execute` 暴露成 public，而是用：

```cpp
executor.step(state, program);
executor.run(state, program);
```

原因是 `step` 可以精确表达“一次 CPU 周期级的行为”，后面做调试器、单步执行、timer interrupt、调度抢占时都需要这个粒度。

### RIP 是什么？为什么 HALT 后还会 advance？

RIP 是 instruction pointer。真实 x86-64 里 RIP 保存下一条要取的指令地址。当前阶段还没有字节级内存和机器码长度，所以先把它简化成“指令数组下标”：

```text
RIP = 0 -> program[0]
RIP = 1 -> program[1]
RIP = 2 -> program[2]
```

普通指令执行完：

```cpp
state.advance_rip();
```

跳转指令执行完：

```cpp
state.set_rip(target);
```

`HALT` 当前实现先让 RIP 指向下一条位置，再标记 halted：

```cpp
state.advance_rip();
state.halt();
```

这样 trace 里能看到“HALT 这条指令已经被执行，RIP 已经移动到逻辑上的下一条”。这对调试和后续异常/中断模型都更容易解释。

### ADD/SUB/CMP 为什么要更新 flags？

CPU 不只是算结果。很多控制流依赖 flags。

例如：

```asm
cmp rax, 1
je  target
```

`CMP` 本质上做一次减法：

```text
result = rax - 1
```

但它不把 result 写回 `rax`，只更新 flags：

```text
ZF = result 是否为 0
SF = result 最高位是否为 1
CF = 无符号减法是否发生借位
OF = 有符号减法是否发生溢出
```

然后 `JE` 读取 `ZF`：

```cpp
if (state.flags().read(FlagId::ZF))
{
    jump_to(target);
}
```

这就是硬件层面的“条件判断”：高级语言里的 `if (a == b)`，编译后通常会变成 `cmp + 条件跳转` 这一类模式。

### 为什么执行器不用继承和虚函数？

可以把每条指令写成一个类：

```cpp
class InstructionExecutor
{
public:
    virtual void execute(CpuState& state) = 0;
};
```

但 CPU 执行是热路径。每条指令都走虚函数，会引入间接跳转，也更难让编译器优化分支。当前项目使用：

```cpp
switch (instruction.opcode())
{
case Opcode::MOV:
    execute_mov(...);
    return;
case Opcode::ADD:
    execute_add(...);
    return;
}
```

这更接近解释器常见的直接分发方式：简单、快、缓存友好，也方便后面继续演进到 decode table 或 computed goto 这类更高级技巧。

### 为什么 trace 是可选指针？

执行 trace 很适合学习和调试：

```text
cycle=1 rip=0 opcode=MOV  rip_after=1
cycle=2 rip=1 opcode=ADD  rip_after=2
cycle=3 rip=2 opcode=HALT rip_after=3 halted=true
```

但正常跑程序时，每条指令都记录 trace 会产生额外内存写入和 vector 增长。当前接口是：

```cpp
executor.run(state, program, max_steps, &trace);  // 需要调试时记录
executor.run(state, program);                     // 热路径不记录
```

这是一种很实用的解耦：调试能力存在，但不强迫所有执行都付出成本。

### 它和后面的线程/进程有什么关系？

线程上下文切换，本质是保存当前 CPU 状态，再恢复另一个线程的 CPU 状态：

```cpp
struct ThreadContext
{
    CpuState cpu_state;
};
```

调度器后面做的事情可以理解成：

```text
1. 当前线程运行若干 step。
2. timer interrupt 或主动 yield 触发调度。
3. 保存当前 CpuState。
4. 从 ready queue 选择下一个线程。
5. 恢复下一个线程的 CpuState。
6. 继续执行 step。
```

进程会比线程多一层地址空间：

```text
Thread = CPU 上下文 + 栈 + 调度状态
Process = 一个或多个 Thread + 虚拟地址空间 + 资源表
```

所以当前这一步不是偏离 OS，而是在给 OS 的线程/进程模型打硬件基础。

## 8. 为什么要做 PhysicalMemory 和 MemoryBus？

因为线程栈、锁变量、信号量计数、生产者消费者队列，本质上都要落到内存读写。没有内存模型，OS 里的这些概念就只能停留在“对象之间调用函数”，而不是“CPU 真的从某个地址读写数据”。

当前项目把内存拆成两层：

```text
PhysicalMemory = 真正保存字节的物理内存
MemoryBus      = CPU 访问内存的总线边界
```

执行器看到的是 `MemoryBus`：

```cpp
executor.run(state, program, memory_bus);
```

而不是直接看到 `PhysicalMemory`。这样后面要加 cache、MMIO、页表翻译时，可以把它们插在 bus 后面。

### 小端序是什么？

x86-64 是 little-endian，小端序。意思是一个多字节整数写进内存时，低位字节放在低地址。

例如写入：

```text
0x1122334455667788
```

如果写到地址 `16`，内存布局是：

```text
address 16: 0x88
address 17: 0x77
address 18: 0x66
address 19: 0x55
address 20: 0x44
address 21: 0x33
address 22: 0x22
address 23: 0x11
```

所以 `PhysicalMemory::write_qword` 不是直接把整数对象粗暴拷进去，而是按字节循环写：

```cpp
for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
{
    const std::size_t bit_shift = byte_index * DATA_SIZE_BYTE_BITS;
    bytes[address + byte_index] = static_cast<UBYTE8>((value >> bit_shift) & 0xFF);
}
```

这能明确表达硬件字节序，也避免宿主机平台字节序影响模拟结果。

### `[RBP + displacement]` 是怎么变成地址的？

内存操作数当前建模为：

```cpp
Operand::mem(RegisterId::RBP, displacement, DataSize::QWORD)
```

它表示：

```text
effective_address = register[RBP] + displacement
```

例如：

```text
RBP = 16
displacement = 16
effective address = 32
```

执行：

```asm
mov [rbp + 16], rax
```

模拟器做的事情是：

```text
1. 读 RBP。
2. 加 displacement 得到有效地址。
3. 按 operand 的 DataSize 决定写几个字节。
4. 通过 MemoryBus 写入 PhysicalMemory。
```

负 displacement 也很重要，因为真实栈帧里常见：

```asm
mov [rbp - 8], rax
```

当前执行器按 x86-64 地址宽度做加法，然后交给内存范围检查。这样可以开始模拟栈上局部变量、函数调用帧、线程栈。

### 为什么内存到内存指令要拒绝？

当前执行器限制二元指令最多只能有一个 memory operand：

```text
mov [addr1], [addr2]  // 拒绝
add [addr1], [addr2]  // 拒绝
```

这是为了贴近 x86-64 的普通指令规则。大多数普通二元指令不能两个操作数都直接来自内存，因为 CPU 执行单元通常需要先把一个值取进寄存器或内部临时路径。

正确写法更接近：

```asm
mov rax, [addr2]
mov [addr1], rax
```

这个限制对学习很重要：寄存器不是“可有可无”，它是 CPU 执行数据流的核心中转位置。

### 这和线程/进程有什么关系？

有了内存以后，线程就不只是一个 `CpuState`。它还需要栈：

```text
Thread = CPU 上下文 + 栈内存范围 + 调度状态
```

有了内存以后，进程也能继续变成：

```text
Process = 线程集合 + 地址空间 + 资源表
```

下一阶段如果做线程，可以让每个线程拥有不同的栈范围：

```text
thread A stack: [0x1000, 0x2000)
thread B stack: [0x2000, 0x3000)
```

调度器切换线程时，不只是恢复寄存器，也会让 `RSP/RBP` 指向对应线程的栈。这样你会真正看到：线程切换不是抽象魔法，而是 CPU 上下文和内存地址一起切换。

## 总结

几个核心判断：

1. `const` 是“不能改”，`constexpr` 是“能编译期求值”。
2. `std::string_view` 是视图，不拥有字符串；生命周期一定要想清楚。
3. `std::string` 是拥有资源的对象，不要把它当普通字面量。
4. `inline` 现代意义主要是 ODR/linkage，不是强制内联优化。
5. `static_cast` 是编译器检查的显式转换；有没有运行时成本取决于转换本身。
6. `static` 的含义取决于位置：链接、生命周期、类成员归属分别是不同概念。
7. CPU 单核执行循环是线程/进程/多核调度的底层基础，不是和 OS 目标相反的方向。
8. PhysicalMemory/MemoryBus 把 CPU 指令执行和底层字节内存连接起来，是后续栈、线程、进程、锁和缓存模型的基础。
