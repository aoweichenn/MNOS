//
// Created by aoweichen on 2026/6/6.
//

#pragma once


#include <cstddef>
#include <string_view>
#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos
{
enum class OperandKind : std::uint8_t{
    NONE,      // 没有操作数
    REGISTER,  //寄存器操作数
    IMMEDIATE, // 立即数
    MEMORY,    // 内存
    COUNT,     // 尾部计数
};

inline constexpr std::size_t OPERAND_KIND_COUNT = static_cast<std::size_t>(OperandKind::COUNT);

inline constexpr SQWORD64 OPERAND_MEMORY_DEFAULT_DISPLACEMENT = SQWORD64{0};

[[nodiscard]] bool is_operand_kind_valid(OperandKind kind) noexcept;

[[nodiscard]] std::size_t operand_kind_to_index(OperandKind kind) noexcept;

[[nodiscard]] std::string_view operand_kind_to_assembly_name(OperandKind kind) noexcept;

class Operand{
public:
    [[nodiscard]] static Operand none() noexcept;
    [[nodiscard]] static Operand reg(RegisterId id) noexcept;
    [[nodiscard]] static Operand imm(SQWORD64 value) noexcept;
    [[nodiscard]] static Operand mem(RegisterId base_register, SQWORD64 displacement, DataSize data_size);

    [[nodiscard]] OperandKind kind() const noexcept;
    [[nodiscard]] bool is_none() const noexcept;
    [[nodiscard]] bool is_register() const noexcept;
    [[nodiscard]] bool is_immediate() const noexcept;
    [[nodiscard]] bool is_memory() const noexcept;

    [[nodiscard]] RegisterId register_id() const;
    [[nodiscard]] SQWORD64 immediate_value() const;
    [[nodiscard]] RegisterId memory_base_register() const;
    [[nodiscard]] SQWORD64 memory_displacement() const;
    [[nodiscard]] DataSize memory_data_size() const;

private:
    OperandKind _kind = OperandKind::NONE;
    RegisterId _register_id = RegisterId::COUNT;
    SQWORD64 _immediate_value = SQWORD64{0};
    RegisterId _memory_base_register = RegisterId::COUNT;
    SQWORD64 _memory_displacement = OPERAND_MEMORY_DEFAULT_DISPLACEMENT;
    DataSize _memory_data_size = DataSize::QWORD;
};
}
