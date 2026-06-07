#pragma once

#include <cstdint>

namespace mnos::cpu
{
// 参照 x86-64 的数据大小命名
using UBYTE8 = std::uint8_t;
using UWORD16 = std::uint16_t;
using UDWORD32 = std::uint32_t;
using UQWORD64 = std::uint64_t;

// 有符号
using SBYTE8 = std::int8_t;
using SWORD16 = std::int16_t;
using SDWORD32 = std::int32_t;
using SQWORD64 = std::int64_t;

// 64 位地址
using ADDRESS64 = UQWORD64;

// RIP 为指令指针寄存器
using RIP64 = UQWORD64;
}
