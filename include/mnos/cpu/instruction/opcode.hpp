#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos::cpu
{
enum class Opcode : std::uint8_t
{
    MOV,
    MOVSX,
    MOVZX,
    LEA,
    ADD,
    SUB,
    CMP,
    INC,
    DEC,
    AND,
    OR,
    XOR,
    TEST,
    CMPXCHG,
    XADD,
    MFENCE,
    INVLPG,
    PUSH,
    POP,
    CALL,
    RET,
    JMP,
    JE,
    JNE,
    JCC,
    SETCC,
    CMOVCC,
    INT,
    SYSCALL,
    SYSRET,
    IRET,
    HLT,
    COUNT
};

inline constexpr std::size_t OPCODE_COUNT = static_cast<std::size_t>(Opcode::COUNT);

[[nodiscard]] bool is_opcode_valid(Opcode opcode) noexcept;

[[nodiscard]] std::size_t opcode_to_index(Opcode opcode) noexcept;

[[nodiscard]] std::string_view opcode_to_assembly_name(Opcode opcode) noexcept;
}
