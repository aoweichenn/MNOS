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
constexpr const char* OPERAND_MEMORY_INVALID_INDEX_REGISTER_MESSAGE = "operand memory index register id is invalid";
constexpr const char* OPERAND_MEMORY_INVALID_DATA_SIZE_MESSAGE = "operand memory data size is invalid";
constexpr const char* OPERAND_MEMORY_INVALID_SCALE_MESSAGE = "operand memory scale must be 1, 2, 4, or 8";
constexpr const char* OPERAND_MEMORY_BASE_REGISTER_MISSING_MESSAGE = "operand memory address has no base register";
constexpr const char* OPERAND_MEMORY_INDEX_REGISTER_MISSING_MESSAGE = "operand memory address has no index register";
constexpr const char* OPERAND_MEMORY_ABSOLUTE_ADDRESS_MISSING_MESSAGE =
    "operand memory address has no absolute address";

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

    static void require_memory_scale(const std::uint8_t scale)
    {
        if (scale != mnos::cpu::MEMORY_ADDRESS_SCALE_1 && scale != mnos::cpu::MEMORY_ADDRESS_SCALE_2 &&
            scale != mnos::cpu::MEMORY_ADDRESS_SCALE_4 && scale != mnos::cpu::MEMORY_ADDRESS_SCALE_8)
        {
            throw std::out_of_range{OPERAND_MEMORY_INVALID_SCALE_MESSAGE};
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

MemoryAddress::MemoryAddress(
    std::optional<RegisterId> base_register,
    std::optional<RegisterId> index_register,
    const std::uint8_t scale,
    const SignedQword displacement,
    std::optional<Address64> absolute_address) noexcept :
    base_register_(base_register),
    index_register_(index_register), scale_(scale), displacement_(displacement), absolute_address_(absolute_address)
{
}

MemoryAddress MemoryAddress::base_displacement(const RegisterId base_register, const SignedQword displacement)
{
    OperandValidator::require_register_id(base_register, OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE);
    return MemoryAddress{
        base_register,
        std::nullopt,
        MEMORY_ADDRESS_SCALE_1,
        displacement,
        std::nullopt};
}

MemoryAddress MemoryAddress::base_index_displacement(
    const RegisterId base_register,
    const RegisterId index_register,
    const std::uint8_t scale,
    const SignedQword displacement)
{
    OperandValidator::require_register_id(base_register, OPERAND_MEMORY_INVALID_BASE_REGISTER_MESSAGE);
    OperandValidator::require_register_id(index_register, OPERAND_MEMORY_INVALID_INDEX_REGISTER_MESSAGE);
    OperandValidator::require_memory_scale(scale);
    return MemoryAddress{base_register, index_register, scale, displacement, std::nullopt};
}

MemoryAddress MemoryAddress::index_displacement(
    const RegisterId index_register,
    const std::uint8_t scale,
    const SignedQword displacement)
{
    OperandValidator::require_register_id(index_register, OPERAND_MEMORY_INVALID_INDEX_REGISTER_MESSAGE);
    OperandValidator::require_memory_scale(scale);
    return MemoryAddress{std::nullopt, index_register, scale, displacement, std::nullopt};
}

MemoryAddress MemoryAddress::absolute(const Address64 address) noexcept
{
    return MemoryAddress{
        std::nullopt,
        std::nullopt,
        MEMORY_ADDRESS_SCALE_1,
        OPERAND_MEMORY_DEFAULT_DISPLACEMENT,
        address};
}

bool MemoryAddress::has_base_register() const noexcept
{
    return this->base_register_.has_value();
}

bool MemoryAddress::has_index_register() const noexcept
{
    return this->index_register_.has_value();
}

bool MemoryAddress::has_absolute_address() const noexcept
{
    return this->absolute_address_.has_value();
}

RegisterId MemoryAddress::base_register() const
{
    if (!this->base_register_.has_value())
    {
        throw std::logic_error{OPERAND_MEMORY_BASE_REGISTER_MISSING_MESSAGE};
    }
    return this->base_register_.value();
}

RegisterId MemoryAddress::index_register() const
{
    if (!this->index_register_.has_value())
    {
        throw std::logic_error{OPERAND_MEMORY_INDEX_REGISTER_MISSING_MESSAGE};
    }
    return this->index_register_.value();
}

std::uint8_t MemoryAddress::scale() const noexcept
{
    return this->scale_;
}

SignedQword MemoryAddress::displacement() const noexcept
{
    return this->displacement_;
}

Address64 MemoryAddress::absolute_address() const
{
    if (!this->absolute_address_.has_value())
    {
        throw std::logic_error{OPERAND_MEMORY_ABSOLUTE_ADDRESS_MISSING_MESSAGE};
    }
    return this->absolute_address_.value();
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

Operand Operand::mem(MemoryAddress address, const DataSize data_size)
{
    OperandValidator::require_memory_data_size(data_size);
    return Operand{MemoryPayload{std::move(address), data_size}};
}

Operand Operand::mem(const RegisterId base_register, const SignedQword displacement, const DataSize data_size)
{
    return Operand::mem(MemoryAddress::base_displacement(base_register, displacement), data_size);
}

Operand Operand::indexed_mem(
    const RegisterId base_register,
    const RegisterId index_register,
    const std::uint8_t scale,
    const SignedQword displacement,
    const DataSize data_size)
{
    return Operand::mem(
        MemoryAddress::base_index_displacement(
            base_register,
            index_register,
            scale,
            displacement),
        data_size);
}

Operand Operand::absolute_mem(const Address64 address, const DataSize data_size)
{
    return Operand::mem(MemoryAddress::absolute(address), data_size);
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

const MemoryAddress& Operand::memory_address() const
{
    return this->payload<MemoryPayload>().address;
}

RegisterId Operand::memory_base_register() const
{
    return this->memory_address().base_register();
}

RegisterId Operand::memory_index_register() const
{
    return this->memory_address().index_register();
}

bool Operand::memory_has_base_register() const
{
    return this->memory_address().has_base_register();
}

bool Operand::memory_has_index_register() const
{
    return this->memory_address().has_index_register();
}

bool Operand::memory_has_absolute_address() const
{
    return this->memory_address().has_absolute_address();
}

std::uint8_t Operand::memory_scale() const
{
    return this->memory_address().scale();
}

DataSize Operand::memory_data_size() const
{
    return this->payload<MemoryPayload>().data_size;
}

SignedQword Operand::memory_displacement() const
{
    return this->memory_address().displacement();
}

Address64 Operand::memory_absolute_address() const
{
    return this->memory_address().absolute_address();
}
}
