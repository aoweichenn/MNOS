#pragma once

#include <optional>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/system/descriptor_tables.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_frame.hpp>

namespace mnos::cpu::system
{
inline constexpr Qword SYSCALL_DEFAULT_RFLAGS_MASK = Qword{1} << FLAG_ID_IF_BIT_INDEX;

class SyscallDescriptor final
{
public:
    [[nodiscard]] static SyscallDescriptor disabled() noexcept;
    [[nodiscard]] static SyscallDescriptor enabled(
        Address64 entry_rip,
        Qword rflags_mask = SYSCALL_DEFAULT_RFLAGS_MASK);

    [[nodiscard]] bool is_enabled() const noexcept;
    [[nodiscard]] Address64 entry_rip() const noexcept;
    [[nodiscard]] Qword rflags_mask() const noexcept;

private:
    SyscallDescriptor(bool enabled, Address64 entry_rip, Qword rflags_mask) noexcept;

    bool enabled_ = false;
    Address64 entry_rip_ = Address64{0};
    Qword rflags_mask_ = Qword{0};
};

class TrapController final
{
public:
    [[nodiscard]] GlobalDescriptorTable& gdt() noexcept;
    [[nodiscard]] const GlobalDescriptorTable& gdt() const noexcept;
    void load_gdt(GlobalDescriptorTable table) noexcept;

    [[nodiscard]] TaskStateSegment& tss() noexcept;
    [[nodiscard]] const TaskStateSegment& tss() const noexcept;
    void load_task_state_segment(TaskStateSegment table) noexcept;

    [[nodiscard]] InterruptDescriptorTable& idt() noexcept;
    [[nodiscard]] const InterruptDescriptorTable& idt() const noexcept;
    void load_idt(InterruptDescriptorTable table) noexcept;

    [[nodiscard]] const SyscallDescriptor& syscall_descriptor() const noexcept;
    void configure_syscall(SyscallDescriptor descriptor) noexcept;

    [[nodiscard]] TrapFrame raise_exception(
        CpuState& state,
        InterruptVector vector,
        std::optional<Qword> error_code = std::nullopt) const;
    [[nodiscard]] TrapFrame raise_interrupt(CpuState& state, InterruptVector vector) const;
    [[nodiscard]] TrapFrame raise_software_interrupt(
        CpuState& state,
        InterruptVector vector,
        InstructionPointer return_rip) const;
    [[nodiscard]] TrapFrame enter_syscall(CpuState& state, InstructionPointer return_rip) const;
    void restore_trap_frame(CpuState& state, const TrapFrame& frame) const;
    void return_from_trap(CpuState& state) const;
    void return_from_syscall(CpuState& state) const;

private:
    [[nodiscard]] TrapFrame dispatch_gate_trap(
        CpuState& state,
        TrapKind kind,
        InterruptVector vector,
        InstructionPointer return_rip,
        std::optional<Qword> error_code) const;
    void switch_to_gate_privilege(CpuState& state, const InterruptGate& gate) const;
    void restore_from_frame(CpuState& state, const TrapFrame& frame) const;

    GlobalDescriptorTable gdt_;
    TaskStateSegment tss_;
    InterruptDescriptorTable idt_;
    SyscallDescriptor syscall_descriptor_ = SyscallDescriptor::disabled();
};
}
