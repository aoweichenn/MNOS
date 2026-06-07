#pragma once

#include <cstddef>
#include <cstdint>
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

[[nodiscard]] bool is_operand_kind_valid(OperandKind kind) noexcept;

[[nodiscard]] std::size_t operand_kind_to_index(OperandKind kind) noexcept;

[[nodiscard]] std::string_view operand_kind_to_assembly_name(OperandKind kind) noexcept;

class Operand
{
public:
    [[nodiscard]] static Operand none() noexcept;
    [[nodiscard]] static Operand reg(RegisterId id);
    [[nodiscard]] static Operand imm(SignedQword value) noexcept;
    [[nodiscard]] static Operand mem(RegisterId base_register, SignedQword displacement, DataSize data_size);

    [[nodiscard]] OperandKind kind() const noexcept;
    [[nodiscard]] bool is_none() const noexcept;
    [[nodiscard]] bool is_register() const noexcept;
    [[nodiscard]] bool is_immediate() const noexcept;
    [[nodiscard]] bool is_memory() const noexcept;

    [[nodiscard]] RegisterId register_id() const;
    [[nodiscard]] SignedQword immediate_value() const;
    [[nodiscard]] RegisterId memory_base_register() const;
    [[nodiscard]] SignedQword memory_displacement() const;
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
        RegisterId base_register;
        SignedQword displacement;
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
