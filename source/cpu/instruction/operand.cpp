//
// Created by aoweichen on 2026/6/6.
//
#include <array>
#include <stdexcept>
#include <mnos/cpu/instruction/operand.hpp>

namespace
{
constexpr std::string_view OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr std::array<std::string_view, mnos::OPERAND_KIND_COUNT> OPERAND_KIND_ASSEMBLY_NAME_TABLE{
    "NONE",
    "REGISTER",
    "IMMEDIATE",
    "MEMORY"
};
constexpr const char* OPERAND_ACCESS_INVALID_KIND_MESSAGES = "Operand invalid kind access";
constexpr const char* OPERAND_REGISTER_INVALID_ID_MESSAGE = "Operand invalid register id";
constexpr const char* OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE = "Operand invalid memory base register";
constexpr const char* OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE = "Operand invalid memory data size";

class Helper{
public:
    [[nodiscard]] constexpr static std::size_t operand_kind_to_index(mnos::OperandKind kind) noexcept
    {
        return static_cast<std::size_t>(kind);
    }

    static void validate_register_id(const mnos::RegisterId id)
    {
        if (mnos::is_register_id_valid(id))
        {
            throw std::out_of_range{OPERAND_REGISTER_INVALID_ID_MESSAGE};
        }
    }

    static void validate_memory_base_register(const mnos::RegisterId id)
    {
        if (mnos::is_register_id_valid(id))
        {
            throw std::out_of_range{OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE};
        }
    }

    static void validate_memory_data_size(const mnos::DataSize size)
    {
        if (!mnos::is_datat_size_valid(size))
        {
            throw std::out_of_range{OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE};
        }
    }
};
}

namespace mnos
{
bool is_operand_kind_valid(const OperandKind kind) noexcept
{
    return Helper::operand_kind_to_index(kind) < OPERAND_KIND_COUNT;
}

std::size_t operand_kind_to_index(const OperandKind kind) noexcept
{
    return Helper::operand_kind_to_index(kind);
}

std::string_view operand_kind_to_assembly_name(const OperandKind kind) noexcept
{
    if (!is_operand_kind_valid(kind))
    {
        return OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT;
    }
    return OPERAND_KIND_ASSEMBLY_NAME_TABLE[Helper::operand_kind_to_index(kind)];
}

Operand Operand::none() noexcept
{
    return Operand{};
}

Operand Operand::reg(const RegisterId id) noexcept
{
    Helper::validate_register_id(id);

    Operand operand;
    operand._kind = OperandKind::REGISTER;
    operand._register_id = id;

    return operand;
}

Operand Operand::imm(const SQWORD64 value) noexcept
{
    Operand operand;
    operand._kind = OperandKind::IMMEDIATE;
    operand._immediate_value = value;

    return operand;
}

Operand Operand::mem(const RegisterId base_register, const SQWORD64 displacement, const DataSize data_size)
{
    Helper::validate_memory_base_register(base_register);
    Helper::validate_memory_data_size(data_size);

    Operand operand;
    operand._kind = OperandKind::MEMORY;
    operand._memory_base_register = base_register;
    operand._memory_displacement = displacement;
    operand._memory_data_size = data_size;

    return operand;
}

OperandKind Operand::kind() const noexcept
{
    return this->_kind;
}

bool Operand::is_none() const noexcept
{
    return this->_kind == OperandKind::NONE;
}

bool Operand::is_register() const noexcept
{
    return this->_kind == OperandKind::REGISTER;
}

bool Operand::is_immediate() const noexcept
{
    return this->_kind == OperandKind::IMMEDIATE;
}

bool Operand::is_memory() const noexcept
{
    return this->_kind == OperandKind::MEMORY;
}

RegisterId Operand::register_id() const
{
    if (!this->is_register())
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGES};
    }
    return this->_register_id;
}

SQWORD64 Operand::immediate_value() const
{
    if (this->is_immediate())
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGES};
    }
    return this->_immediate_value;
}

RegisterId Operand::memory_base_register() const
{
    if (this->is_memory())
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGES};
    }
    return this->_memory_base_register;
}

DataSize Operand::memory_data_size() const
{
    if (this->is_memory())
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGES};
    }
    return this->_memory_data_size;
}

SQWORD64 Operand::memory_displacement() const
{
    if (this->is_memory())
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGES};
    }
    return this->_memory_displacement;
}
}
