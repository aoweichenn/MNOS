//
// Created by aoweichen on 2026/6/6.
//

#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace mnos
{
enum class Opcode : std::uint8_t{
    MOV,
    ADD,
    SUB,
    CMP,
    JMP,
    JE,
    JNE,
    HALT,
    COUNT
};

inline constexpr std::size_t OPCODE_INSTRUCTION_KIND_COUNT = static_cast<std::size_t>(Opcode::COUNT);

[[nodiscard]] bool is_opcode_valid(Opcode opcode) noexcept;

[[nodiscard]] std::size_t opcode_to_index(Opcode opcode) noexcept;

[[nodiscard]] std::string_view opcode_to_assembly_name(Opcode opcode) noexcept;
}
