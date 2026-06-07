#pragma once

#include <cstdint>

namespace mnos::cpu
{
using Byte = std::uint8_t;
using Word = std::uint16_t;
using Dword = std::uint32_t;
using Qword = std::uint64_t;

using SignedByte = std::int8_t;
using SignedWord = std::int16_t;
using SignedDword = std::int32_t;
using SignedQword = std::int64_t;

using Address64 = Qword;
using InstructionPointer = Qword;
using CycleCount = Qword;
}
