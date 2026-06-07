#include <array>
#include <stdexcept>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/system/descriptor_tables.hpp>

namespace
{
constexpr std::string_view SYSTEM_TABLE_INVALID_NAME = "<invalid>";
constexpr const char* SYSTEM_TABLE_INVALID_GATE_TYPE_MESSAGE = "interrupt gate type is invalid";
constexpr const char* SYSTEM_TABLE_INVALID_SEGMENT_DESCRIPTOR_KIND_MESSAGE = "segment descriptor kind is invalid";
constexpr const char* SYSTEM_TABLE_INVALID_PRIVILEGE_LEVEL_MESSAGE = "system table privilege level is invalid";
constexpr const char* SYSTEM_TABLE_IDT_GATE_NOT_PRESENT_MESSAGE = "interrupt descriptor table gate is not present";
constexpr const char* SYSTEM_TABLE_GDT_SELECTOR_OUT_OF_RANGE_MESSAGE = "gdt selector index is out of range";
constexpr const char* SYSTEM_TABLE_GDT_DESCRIPTOR_NOT_PRESENT_MESSAGE = "gdt descriptor is not present";
constexpr const char* SYSTEM_TABLE_TSS_RING3_STACK_MESSAGE = "tss does not provide a ring3 privilege stack";
constexpr const char* SYSTEM_TABLE_TSS_STACK_NOT_PRESENT_MESSAGE = "tss privilege stack is not present";

class GateTypeCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::system::GateType type) noexcept
    {
        return NAMES.contains(type);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::system::GateType type) noexcept
    {
        return NAMES.index(type);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::system::GateType type) noexcept
    {
        return NAMES.name(type);
    }

private:
    inline static constexpr auto NAMES = mnos::core::make_enum_name_table<mnos::cpu::system::GateType>(
        std::array<std::string_view, mnos::cpu::system::GATE_TYPE_COUNT>{"INTERRUPT_GATE", "TRAP_GATE"},
        SYSTEM_TABLE_INVALID_NAME);
};

class SegmentDescriptorKindCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::system::SegmentDescriptorKind kind) noexcept
    {
        return NAMES.contains(kind);
    }

    [[nodiscard]] static std::size_t index(const mnos::cpu::system::SegmentDescriptorKind kind) noexcept
    {
        return NAMES.index(kind);
    }

    [[nodiscard]] static std::string_view name(const mnos::cpu::system::SegmentDescriptorKind kind) noexcept
    {
        return NAMES.name(kind);
    }

private:
    inline static constexpr auto NAMES =
        mnos::core::make_enum_name_table<mnos::cpu::system::SegmentDescriptorKind>(
            std::array<std::string_view, mnos::cpu::system::SEGMENT_DESCRIPTOR_KIND_COUNT>{"CODE", "DATA", "TSS"},
            SYSTEM_TABLE_INVALID_NAME);
};

void require_privilege_level(const mnos::cpu::system::PrivilegeLevel level)
{
    if (!mnos::cpu::system::is_privilege_level_valid(level))
    {
        throw std::out_of_range{SYSTEM_TABLE_INVALID_PRIVILEGE_LEVEL_MESSAGE};
    }
}

void require_gate_type(const mnos::cpu::system::GateType type)
{
    if (!mnos::cpu::system::is_gate_type_valid(type))
    {
        throw std::out_of_range{SYSTEM_TABLE_INVALID_GATE_TYPE_MESSAGE};
    }
}

void require_gdt_index(const std::uint16_t index)
{
    if (index >= mnos::cpu::system::GDT_DESCRIPTOR_COUNT)
    {
        throw std::out_of_range{SYSTEM_TABLE_GDT_SELECTOR_OUT_OF_RANGE_MESSAGE};
    }
}
}

namespace mnos::cpu::system
{
bool is_gate_type_valid(const GateType type) noexcept
{
    return GateTypeCatalog::contains(type);
}

std::size_t gate_type_to_index(const GateType type) noexcept
{
    return GateTypeCatalog::index(type);
}

std::string_view gate_type_to_name(const GateType type) noexcept
{
    return GateTypeCatalog::name(type);
}

InterruptGate InterruptGate::absent() noexcept
{
    return InterruptGate{false, GateType::INTERRUPT_GATE, Address64{0}, PrivilegeLevel::RING0};
}

InterruptGate InterruptGate::interrupt_gate(
    const Address64 handler_rip,
    const PrivilegeLevel target_privilege_level)
{
    return InterruptGate{true, GateType::INTERRUPT_GATE, handler_rip, target_privilege_level};
}

InterruptGate InterruptGate::trap_gate(
    const Address64 handler_rip,
    const PrivilegeLevel target_privilege_level)
{
    return InterruptGate{true, GateType::TRAP_GATE, handler_rip, target_privilege_level};
}

InterruptGate::InterruptGate(
    const bool present,
    const GateType type,
    const Address64 handler_rip,
    const PrivilegeLevel target_privilege_level) :
    present_(present),
    type_(type),
    handler_rip_(handler_rip),
    target_privilege_level_(target_privilege_level)
{
    require_gate_type(type);
    require_privilege_level(target_privilege_level);
}

bool InterruptGate::is_present() const noexcept
{
    return this->present_;
}

GateType InterruptGate::type() const noexcept
{
    return this->type_;
}

Address64 InterruptGate::handler_rip() const noexcept
{
    return this->handler_rip_;
}

PrivilegeLevel InterruptGate::target_privilege_level() const noexcept
{
    return this->target_privilege_level_;
}

void InterruptDescriptorTable::set_gate(const InterruptVector vector, InterruptGate gate)
{
    this->gates_.at(vector.index()) = gate;
}

void InterruptDescriptorTable::clear_gate(const InterruptVector vector) noexcept
{
    this->gates_[vector.index()] = InterruptGate::absent();
}

bool InterruptDescriptorTable::has_gate(const InterruptVector vector) const noexcept
{
    return this->gates_[vector.index()].is_present();
}

const InterruptGate& InterruptDescriptorTable::gate_at(const InterruptVector vector) const
{
    const InterruptGate& gate = this->gates_.at(vector.index());
    if (!gate.is_present())
    {
        throw std::out_of_range{SYSTEM_TABLE_IDT_GATE_NOT_PRESENT_MESSAGE};
    }

    return gate;
}

bool is_segment_descriptor_kind_valid(const SegmentDescriptorKind kind) noexcept
{
    return SegmentDescriptorKindCatalog::contains(kind);
}

std::size_t segment_descriptor_kind_to_index(const SegmentDescriptorKind kind) noexcept
{
    return SegmentDescriptorKindCatalog::index(kind);
}

std::string_view segment_descriptor_kind_to_name(const SegmentDescriptorKind kind) noexcept
{
    return SegmentDescriptorKindCatalog::name(kind);
}

SegmentSelector SegmentSelector::from_index(
    const std::uint16_t index,
    const PrivilegeLevel requested_privilege_level)
{
    require_gdt_index(index);
    require_privilege_level(requested_privilege_level);
    const std::uint16_t value =
        static_cast<std::uint16_t>((index << GDT_SELECTOR_INDEX_SHIFT) |
                                   privilege_level_to_ring(requested_privilege_level));
    return SegmentSelector{value};
}

SegmentSelector::value_type SegmentSelector::value() const noexcept
{
    return this->value_;
}

std::uint16_t SegmentSelector::index() const noexcept
{
    return static_cast<std::uint16_t>(this->value_ >> GDT_SELECTOR_INDEX_SHIFT);
}

PrivilegeLevel SegmentSelector::requested_privilege_level() const
{
    return static_cast<PrivilegeLevel>(this->value_ & GDT_SELECTOR_RPL_MASK);
}

bool SegmentSelector::is_null() const noexcept
{
    return this->index() == std::uint16_t{0};
}

SegmentDescriptor SegmentDescriptor::absent() noexcept
{
    return SegmentDescriptor{
        false,
        SegmentDescriptorKind::DATA,
        PrivilegeLevel::RING0,
        Address64{0},
        Dword{0}};
}

SegmentDescriptor SegmentDescriptor::code(const PrivilegeLevel descriptor_privilege_level)
{
    return SegmentDescriptor{
        true,
        SegmentDescriptorKind::CODE,
        descriptor_privilege_level,
        Address64{0},
        Dword{0}};
}

SegmentDescriptor SegmentDescriptor::data(const PrivilegeLevel descriptor_privilege_level)
{
    return SegmentDescriptor{
        true,
        SegmentDescriptorKind::DATA,
        descriptor_privilege_level,
        Address64{0},
        Dword{0}};
}

SegmentDescriptor SegmentDescriptor::tss(const Address64 base_address, const Dword limit_bytes)
{
    return SegmentDescriptor{true, SegmentDescriptorKind::TSS, PrivilegeLevel::RING0, base_address, limit_bytes};
}

SegmentDescriptor::SegmentDescriptor(
    const bool present,
    const SegmentDescriptorKind kind,
    const PrivilegeLevel descriptor_privilege_level,
    const Address64 base_address,
    const Dword limit_bytes) :
    present_(present),
    kind_(kind),
    descriptor_privilege_level_(descriptor_privilege_level),
    base_address_(base_address),
    limit_bytes_(limit_bytes)
{
    if (!is_segment_descriptor_kind_valid(kind))
    {
        throw std::out_of_range{SYSTEM_TABLE_INVALID_SEGMENT_DESCRIPTOR_KIND_MESSAGE};
    }
    require_privilege_level(descriptor_privilege_level);
}

bool SegmentDescriptor::is_present() const noexcept
{
    return this->present_;
}

SegmentDescriptorKind SegmentDescriptor::kind() const noexcept
{
    return this->kind_;
}

PrivilegeLevel SegmentDescriptor::descriptor_privilege_level() const noexcept
{
    return this->descriptor_privilege_level_;
}

Address64 SegmentDescriptor::base_address() const noexcept
{
    return this->base_address_;
}

Dword SegmentDescriptor::limit_bytes() const noexcept
{
    return this->limit_bytes_;
}

void GlobalDescriptorTable::set_descriptor(const SegmentSelector selector, SegmentDescriptor descriptor)
{
    require_gdt_index(selector.index());
    this->descriptors_.at(selector.index()) = descriptor;
}

void GlobalDescriptorTable::clear_descriptor(const SegmentSelector selector) noexcept
{
    this->descriptors_[selector.index()] = SegmentDescriptor::absent();
}

bool GlobalDescriptorTable::has_descriptor(const SegmentSelector selector) const noexcept
{
    return selector.index() < this->descriptors_.size() && this->descriptors_[selector.index()].is_present();
}

const SegmentDescriptor& GlobalDescriptorTable::descriptor_at(const SegmentSelector selector) const
{
    require_gdt_index(selector.index());
    const SegmentDescriptor& descriptor = this->descriptors_.at(selector.index());
    if (!descriptor.is_present())
    {
        throw std::out_of_range{SYSTEM_TABLE_GDT_DESCRIPTOR_NOT_PRESENT_MESSAGE};
    }

    return descriptor;
}

void TaskStateSegment::set_privilege_stack(const PrivilegeLevel level, const Qword stack_pointer)
{
    const std::size_t index = privilege_stack_index(level);
    this->privilege_stacks_[index] = stack_pointer;
    this->privilege_stack_present_[index] = true;
}

void TaskStateSegment::clear_privilege_stack(const PrivilegeLevel level)
{
    const std::size_t index = privilege_stack_index(level);
    this->privilege_stacks_[index] = Qword{0};
    this->privilege_stack_present_[index] = false;
}

bool TaskStateSegment::has_privilege_stack(const PrivilegeLevel level) const
{
    const std::size_t index = privilege_stack_index(level);
    return this->privilege_stack_present_[index];
}

Qword TaskStateSegment::privilege_stack(const PrivilegeLevel level) const
{
    const std::size_t index = privilege_stack_index(level);
    if (!this->privilege_stack_present_[index])
    {
        throw std::out_of_range{SYSTEM_TABLE_TSS_STACK_NOT_PRESENT_MESSAGE};
    }
    return this->privilege_stacks_[index];
}

std::size_t TaskStateSegment::privilege_stack_index(const PrivilegeLevel level)
{
    require_privilege_level(level);
    if (level == PrivilegeLevel::RING3)
    {
        throw std::out_of_range{SYSTEM_TABLE_TSS_RING3_STACK_MESSAGE};
    }
    return privilege_level_to_index(level);
}
}
