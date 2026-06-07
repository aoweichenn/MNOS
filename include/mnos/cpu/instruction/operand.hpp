#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos::cpu
{
enum class OperandKind : std::uint8_t
{
    NONE,      // 没有操作数
    REGISTER,  // 寄存器操作数
    IMMEDIATE, // 立即数
    MEMORY,    // 内存
    COUNT,     // 尾部计数
};

inline constexpr std::size_t OPERAND_KIND_COUNT = static_cast<std::size_t>(OperandKind::COUNT);

inline constexpr SignedQword OPERAND_MEMORY_DEFAULT_DISPLACEMENT = SignedQword{0};
inline constexpr std::uint8_t MEMORY_ADDRESS_SCALE_1 = 1;
inline constexpr std::uint8_t MEMORY_ADDRESS_SCALE_2 = 2;
inline constexpr std::uint8_t MEMORY_ADDRESS_SCALE_4 = 4;
inline constexpr std::uint8_t MEMORY_ADDRESS_SCALE_8 = 8;

[[nodiscard]] bool is_operand_kind_valid(OperandKind kind) noexcept;

[[nodiscard]] std::size_t operand_kind_to_index(OperandKind kind) noexcept;

[[nodiscard]] std::string_view operand_kind_to_assembly_name(OperandKind kind) noexcept;

class MemoryAddress
{
public:
    [[nodiscard]] static MemoryAddress base_displacement(RegisterId base_register, SignedQword displacement);
    [[nodiscard]] static MemoryAddress base_index_displacement(
        RegisterId base_register,
        RegisterId index_register,
        std::uint8_t scale,
        SignedQword displacement);
    [[nodiscard]] static MemoryAddress index_displacement(
        RegisterId index_register,
        std::uint8_t scale,
        SignedQword displacement);
    [[nodiscard]] static MemoryAddress absolute(Address64 address) noexcept;

    [[nodiscard]] bool has_base_register() const noexcept;
    [[nodiscard]] bool has_index_register() const noexcept;
    [[nodiscard]] bool has_absolute_address() const noexcept;
    [[nodiscard]] RegisterId base_register() const;
    [[nodiscard]] RegisterId index_register() const;
    [[nodiscard]] std::uint8_t scale() const noexcept;
    [[nodiscard]] SignedQword displacement() const noexcept;
    [[nodiscard]] Address64 absolute_address() const;

private:
    MemoryAddress(
        std::optional<RegisterId> base_register,
        std::optional<RegisterId> index_register,
        std::uint8_t scale,
        SignedQword displacement,
        std::optional<Address64> absolute_address) noexcept;

    std::optional<RegisterId> base_register_;
    std::optional<RegisterId> index_register_;
    std::uint8_t scale_ = MEMORY_ADDRESS_SCALE_1;
    SignedQword displacement_ = OPERAND_MEMORY_DEFAULT_DISPLACEMENT;
    std::optional<Address64> absolute_address_;
};

class Operand
{
public:
    [[nodiscard]] static Operand none() noexcept;
    [[nodiscard]] static Operand reg(RegisterId id);
    [[nodiscard]] static Operand imm(SignedQword value) noexcept;
    [[nodiscard]] static Operand mem(MemoryAddress address, DataSize data_size);
    [[nodiscard]] static Operand mem(RegisterId base_register, SignedQword displacement, DataSize data_size);
    [[nodiscard]] static Operand indexed_mem(
        RegisterId base_register,
        RegisterId index_register,
        std::uint8_t scale,
        SignedQword displacement,
        DataSize data_size);
    [[nodiscard]] static Operand absolute_mem(Address64 address, DataSize data_size);

    [[nodiscard]] OperandKind kind() const noexcept;
    [[nodiscard]] bool is_none() const noexcept;
    [[nodiscard]] bool is_register() const noexcept;
    [[nodiscard]] bool is_immediate() const noexcept;
    [[nodiscard]] bool is_memory() const noexcept;

    [[nodiscard]] RegisterId register_id() const;
    [[nodiscard]] SignedQword immediate_value() const;
    [[nodiscard]] const MemoryAddress& memory_address() const;
    [[nodiscard]] RegisterId memory_base_register() const;
    [[nodiscard]] RegisterId memory_index_register() const;
    [[nodiscard]] bool memory_has_base_register() const;
    [[nodiscard]] bool memory_has_index_register() const;
    [[nodiscard]] bool memory_has_absolute_address() const;
    [[nodiscard]] std::uint8_t memory_scale() const;
    [[nodiscard]] SignedQword memory_displacement() const;
    [[nodiscard]] Address64 memory_absolute_address() const;
    [[nodiscard]] DataSize memory_data_size() const;

private:
    struct RegisterPayload
    {
        RegisterId id;
    };

    struct ImmediatePayload
    {
        SignedQword value;
    };

    struct MemoryPayload
    {
        MemoryAddress address;
        DataSize data_size;
    };

    using Storage = std::variant<std::monostate, RegisterPayload, ImmediatePayload, MemoryPayload>;

    static constexpr std::size_t OPERAND_STORAGE_NONE_INDEX = 0;
    static constexpr std::size_t OPERAND_STORAGE_REGISTER_INDEX = 1;
    static constexpr std::size_t OPERAND_STORAGE_IMMEDIATE_INDEX = 2;
    static constexpr std::size_t OPERAND_STORAGE_MEMORY_INDEX = 3;

    static_assert(static_cast<std::size_t>(OperandKind::NONE) == OPERAND_STORAGE_NONE_INDEX);
    static_assert(static_cast<std::size_t>(OperandKind::REGISTER) == OPERAND_STORAGE_REGISTER_INDEX);
    static_assert(static_cast<std::size_t>(OperandKind::IMMEDIATE) == OPERAND_STORAGE_IMMEDIATE_INDEX);
    static_assert(static_cast<std::size_t>(OperandKind::MEMORY) == OPERAND_STORAGE_MEMORY_INDEX);
    static_assert(std::variant_size_v<Storage> == OPERAND_KIND_COUNT);

    explicit Operand(Storage storage) noexcept;

    template <typename Payload>
    [[nodiscard]] const Payload& payload() const;

    Storage storage_;
};
}
