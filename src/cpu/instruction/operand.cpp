#include <array>
#include <stdexcept>
#include <utility>
#include <variant>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/instruction/operand.hpp>

namespace
{
constexpr std::string_view OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr auto OPERAND_KIND_ASSEMBLY_NAME_TABLE = mnos::make_enum_map<mnos::OperandKind>(
    std::array<std::string_view, mnos::OPERAND_KIND_COUNT>{"NONE", "REGISTER", "IMMEDIATE", "MEMORY"});

constexpr const char* OPERAND_ACCESS_INVALID_KIND_MESSAGE = "operand kind does not match requested payload";
constexpr const char* OPERAND_REGISTER_INVALID_ID_MESSAGE = "Operand invalid register id";
constexpr const char* OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE = "Operand invalid memory base register";
constexpr const char* OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE = "Operand invalid memory data size";

void validate_register_id(const mnos::RegisterId id, const char* const message)
{
    if (!mnos::is_register_id_valid(id))
    {
        throw std::out_of_range{message};
    }
}

void validate_memory_data_size(const mnos::DataSize size)
{
    if (!mnos::is_data_size_valid(size))
    {
        throw std::out_of_range{OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE};
    }
}
}

namespace mnos
{
bool is_operand_kind_valid(const OperandKind kind) noexcept
{
    return OPERAND_KIND_ASSEMBLY_NAME_TABLE.contains(kind);
}

std::size_t operand_kind_to_index(const OperandKind kind) noexcept
{
    return enum_to_index(kind);
}

std::string_view operand_kind_to_assembly_name(const OperandKind kind) noexcept
{
    return OPERAND_KIND_ASSEMBLY_NAME_TABLE.value_or(kind, OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT);
}

Operand::Operand(Storage storage) noexcept : storage_(std::move(storage))
{
}

template <typename Payload>
const Payload& Operand::payload() const
{
    const Payload* const payload = std::get_if<Payload>(&this->storage_);
    if (payload == nullptr)
    {
        throw std::logic_error{OPERAND_ACCESS_INVALID_KIND_MESSAGE};
    }
    return *payload;
}

Operand Operand::none() noexcept
{
    return Operand{std::monostate{}};
}

Operand Operand::reg(const RegisterId id)
{
    validate_register_id(id, OPERAND_REGISTER_INVALID_ID_MESSAGE);
    return Operand{RegisterPayload{id}};
}

Operand Operand::imm(const SQWORD64 value) noexcept
{
    return Operand{ImmediatePayload{value}};
}

Operand Operand::mem(const RegisterId base_register, const SQWORD64 displacement, const DataSize data_size)
{
    validate_register_id(base_register, OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE);
    validate_memory_data_size(data_size);
    return Operand{MemoryPayload{base_register, displacement, data_size}};
}

OperandKind Operand::kind() const noexcept
{
    return static_cast<OperandKind>(this->storage_.index());
}

bool Operand::is_none() const noexcept
{
    return std::holds_alternative<std::monostate>(this->storage_);
}

bool Operand::is_register() const noexcept
{
    return std::holds_alternative<RegisterPayload>(this->storage_);
}

bool Operand::is_immediate() const noexcept
{
    return std::holds_alternative<ImmediatePayload>(this->storage_);
}

bool Operand::is_memory() const noexcept
{
    return std::holds_alternative<MemoryPayload>(this->storage_);
}

RegisterId Operand::register_id() const
{
    return this->payload<RegisterPayload>().id;
}

SQWORD64 Operand::immediate_value() const
{
    return this->payload<ImmediatePayload>().value;
}

RegisterId Operand::memory_base_register() const
{
    return this->payload<MemoryPayload>().base_register;
}

DataSize Operand::memory_data_size() const
{
    return this->payload<MemoryPayload>().data_size;
}

SQWORD64 Operand::memory_displacement() const
{
    return this->payload<MemoryPayload>().displacement;
}
}
