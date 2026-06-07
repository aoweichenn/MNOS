#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/instruction/operand.hpp>

namespace
{
constexpr std::string_view OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* OPERAND_ACCESS_INVALID_KIND_MESSAGE = "operand kind does not match requested payload";
constexpr const char* OPERAND_REGISTER_INVALID_ID_MESSAGE = "operand register id is invalid";
constexpr const char* OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE = "operand memory base register id is invalid";
constexpr const char* OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE = "operand memory data size is invalid";

class OperandKindCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::OperandKind kind) noexcept
    {
        return OPERAND_KIND_NAMES.contains(kind);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::OperandKind kind) noexcept
    {
        return OPERAND_KIND_NAMES.index(kind);
    }

    [[nodiscard]] static std::string_view assembly_name(const mnos::cpu::OperandKind kind) noexcept
    {
        return OPERAND_KIND_NAMES.name(kind);
    }

private:
    inline static constexpr auto OPERAND_KIND_NAMES = mnos::core::make_enum_name_table<mnos::cpu::OperandKind>(
        std::array<std::string_view, mnos::cpu::OPERAND_KIND_COUNT>{"NONE", "REGISTER", "IMMEDIATE", "MEMORY"},
        OPERAND_KIND_ASSEMBLY_NAME_INVALID_TEXT);
};

class OperandValidator
{
public:
    static void require_register_id(const mnos::cpu::RegisterId id, const char* const message)
    {
        if (!mnos::cpu::is_register_id_valid(id))
        {
            throw std::out_of_range{message};
        }
    }

    static void require_memory_data_size(const mnos::cpu::DataSize size)
    {
        if (!mnos::cpu::is_data_size_valid(size))
        {
            throw std::out_of_range{OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE};
        }
    }
};
}

namespace mnos::cpu
{
bool is_operand_kind_valid(const OperandKind kind) noexcept
{
    return OperandKindCatalog::contains(kind);
}

std::size_t operand_kind_to_index(const OperandKind kind) noexcept
{
    return OperandKindCatalog::index(kind);
}

std::string_view operand_kind_to_assembly_name(const OperandKind kind) noexcept
{
    return OperandKindCatalog::assembly_name(kind);
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
    OperandValidator::require_register_id(id, OPERAND_REGISTER_INVALID_ID_MESSAGE);
    return Operand{RegisterPayload{id}};
}

Operand Operand::imm(const SignedQword value) noexcept
{
    return Operand{ImmediatePayload{value}};
}

Operand Operand::mem(const RegisterId base_register, const SignedQword displacement, const DataSize data_size)
{
    OperandValidator::require_register_id(base_register, OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE);
    OperandValidator::require_memory_data_size(data_size);
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

SignedQword Operand::immediate_value() const
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

SignedQword Operand::memory_displacement() const
{
    return this->payload<MemoryPayload>().displacement;
}
}
