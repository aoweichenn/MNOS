#pragma once

#include <cstdint>

namespace mnos::cpu
{
using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;

using S8 = std::int8_t;
using S16 = std::int16_t;
using S32 = std::int32_t;
using S64 = std::int64_t;

// 参照 x86-64 的数据大小命名
using UBYTE8 = U8;
using UWORD16 = U16;
using UDWORD32 = U32;
using UQWORD64 = U64;

// 有符号
using SBYTE8 = S8;
using SWORD16 = S16;
using SDWORD32 = S32;
using SQWORD64 = S64;

// 64 位地址
using ADDRESS64 = UQWORD64;

// RIP 为指令指针寄存器
using RIP64 = UQWORD64;
}
