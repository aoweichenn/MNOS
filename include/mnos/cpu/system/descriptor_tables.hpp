#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/privilege.hpp>

namespace mnos::cpu::system
{
inline constexpr std::size_t GDT_DESCRIPTOR_COUNT = 16;
inline constexpr std::uint16_t GDT_SELECTOR_INDEX_SHIFT = 3;
inline constexpr std::uint16_t GDT_SELECTOR_RPL_MASK = 0b11;
inline constexpr std::size_t TSS_PRIVILEGE_STACK_COUNT = 3;

enum class GateType : std::uint8_t
{
    INTERRUPT_GATE,
    TRAP_GATE,
    COUNT,
};

inline constexpr std::size_t GATE_TYPE_COUNT = static_cast<std::size_t>(GateType::COUNT);

[[nodiscard]] bool is_gate_type_valid(GateType type) noexcept;
[[nodiscard]] std::size_t gate_type_to_index(GateType type) noexcept;
[[nodiscard]] std::string_view gate_type_to_name(GateType type) noexcept;

class InterruptGate final
{
public:
    InterruptGate() noexcept = default;

    [[nodiscard]] static InterruptGate absent() noexcept;
    [[nodiscard]] static InterruptGate interrupt_gate(
        Address64 handler_rip,
        PrivilegeLevel target_privilege_level = PrivilegeLevel::RING0);
    [[nodiscard]] static InterruptGate trap_gate(
        Address64 handler_rip,
        PrivilegeLevel target_privilege_level = PrivilegeLevel::RING0);

    [[nodiscard]] bool is_present() const noexcept;
    [[nodiscard]] GateType type() const noexcept;
    [[nodiscard]] Address64 handler_rip() const noexcept;
    [[nodiscard]] PrivilegeLevel target_privilege_level() const noexcept;

private:
    InterruptGate(bool present, GateType type, Address64 handler_rip, PrivilegeLevel target_privilege_level);

    bool present_ = false;
    GateType type_ = GateType::INTERRUPT_GATE;
    Address64 handler_rip_ = Address64{0};
    PrivilegeLevel target_privilege_level_ = PrivilegeLevel::RING0;
};

class InterruptDescriptorTable final
{
public:
    void set_gate(InterruptVector vector, InterruptGate gate);
    void clear_gate(InterruptVector vector) noexcept;

    [[nodiscard]] bool has_gate(InterruptVector vector) const noexcept;
    [[nodiscard]] const InterruptGate& gate_at(InterruptVector vector) const;

private:
    std::array<InterruptGate, INTERRUPT_VECTOR_COUNT> gates_{};
};

enum class SegmentDescriptorKind : std::uint8_t
{
    CODE,
    DATA,
    TSS,
    COUNT,
};

inline constexpr std::size_t SEGMENT_DESCRIPTOR_KIND_COUNT =
    static_cast<std::size_t>(SegmentDescriptorKind::COUNT);

[[nodiscard]] bool is_segment_descriptor_kind_valid(SegmentDescriptorKind kind) noexcept;
[[nodiscard]] std::size_t segment_descriptor_kind_to_index(SegmentDescriptorKind kind) noexcept;
[[nodiscard]] std::string_view segment_descriptor_kind_to_name(SegmentDescriptorKind kind) noexcept;

class SegmentSelector final
{
public:
    using value_type = std::uint16_t;

    constexpr SegmentSelector() noexcept = default;

    [[nodiscard]] static SegmentSelector from_index(std::uint16_t index, PrivilegeLevel requested_privilege_level);

    [[nodiscard]] value_type value() const noexcept;
    [[nodiscard]] std::uint16_t index() const noexcept;
    [[nodiscard]] PrivilegeLevel requested_privilege_level() const;
    [[nodiscard]] bool is_null() const noexcept;

private:
    explicit constexpr SegmentSelector(value_type value) noexcept : value_(value)
    {
    }

    value_type value_ = value_type{0};
};

class SegmentDescriptor final
{
public:
    SegmentDescriptor() noexcept = default;

    [[nodiscard]] static SegmentDescriptor absent() noexcept;
    [[nodiscard]] static SegmentDescriptor code(PrivilegeLevel descriptor_privilege_level);
    [[nodiscard]] static SegmentDescriptor data(PrivilegeLevel descriptor_privilege_level);
    [[nodiscard]] static SegmentDescriptor tss(Address64 base_address, Dword limit_bytes);

    [[nodiscard]] bool is_present() const noexcept;
    [[nodiscard]] SegmentDescriptorKind kind() const noexcept;
    [[nodiscard]] PrivilegeLevel descriptor_privilege_level() const noexcept;
    [[nodiscard]] Address64 base_address() const noexcept;
    [[nodiscard]] Dword limit_bytes() const noexcept;

private:
    SegmentDescriptor(
        bool present,
        SegmentDescriptorKind kind,
        PrivilegeLevel descriptor_privilege_level,
        Address64 base_address,
        Dword limit_bytes);

    bool present_ = false;
    SegmentDescriptorKind kind_ = SegmentDescriptorKind::DATA;
    PrivilegeLevel descriptor_privilege_level_ = PrivilegeLevel::RING0;
    Address64 base_address_ = Address64{0};
    Dword limit_bytes_ = Dword{0};
};

class GlobalDescriptorTable final
{
public:
    void set_descriptor(SegmentSelector selector, SegmentDescriptor descriptor);
    void clear_descriptor(SegmentSelector selector) noexcept;

    [[nodiscard]] bool has_descriptor(SegmentSelector selector) const noexcept;
    [[nodiscard]] const SegmentDescriptor& descriptor_at(SegmentSelector selector) const;

private:
    std::array<SegmentDescriptor, GDT_DESCRIPTOR_COUNT> descriptors_{};
};

class TaskStateSegment final
{
public:
    void set_privilege_stack(PrivilegeLevel level, Qword stack_pointer);
    void clear_privilege_stack(PrivilegeLevel level);

    [[nodiscard]] bool has_privilege_stack(PrivilegeLevel level) const;
    [[nodiscard]] Qword privilege_stack(PrivilegeLevel level) const;

private:
    [[nodiscard]] static std::size_t privilege_stack_index(PrivilegeLevel level);

    std::array<Qword, TSS_PRIVILEGE_STACK_COUNT> privilege_stacks_{};
    std::array<bool, TSS_PRIVILEGE_STACK_COUNT> privilege_stack_present_{};
};
}
