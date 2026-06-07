#include <stdexcept>
#include <utility>

#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

namespace
{
constexpr const char* TRAP_CONTROLLER_SYSCALL_DISABLED_MESSAGE = "syscall descriptor is disabled";
constexpr const char* TRAP_CONTROLLER_PENDING_TRAP_REQUIRED_MESSAGE = "trap return requires a pending trap";
constexpr const char* TRAP_CONTROLLER_SYSCALL_TRAP_REQUIRED_MESSAGE = "syscall return requires a pending syscall trap";
constexpr const char* TRAP_CONTROLLER_PRIVILEGE_STACK_REQUIRED_MESSAGE =
    "trap privilege switch requires a configured tss privilege stack";
}

namespace mnos::cpu::system
{
SyscallDescriptor SyscallDescriptor::disabled() noexcept
{
    return SyscallDescriptor{false, Address64{0}, Qword{0}};
}

SyscallDescriptor SyscallDescriptor::enabled(const Address64 entry_rip, const Qword rflags_mask)
{
    return SyscallDescriptor{true, entry_rip, rflags_mask};
}

SyscallDescriptor::SyscallDescriptor(
    const bool enabled,
    const Address64 entry_rip,
    const Qword rflags_mask) noexcept :
    enabled_(enabled),
    entry_rip_(entry_rip),
    rflags_mask_(rflags_mask)
{
}

bool SyscallDescriptor::is_enabled() const noexcept
{
    return this->enabled_;
}

Address64 SyscallDescriptor::entry_rip() const noexcept
{
    return this->entry_rip_;
}

Qword SyscallDescriptor::rflags_mask() const noexcept
{
    return this->rflags_mask_;
}

GlobalDescriptorTable& TrapController::gdt() noexcept
{
    return this->gdt_;
}

const GlobalDescriptorTable& TrapController::gdt() const noexcept
{
    return this->gdt_;
}

void TrapController::load_gdt(GlobalDescriptorTable table) noexcept
{
    this->gdt_ = std::move(table);
}

TaskStateSegment& TrapController::tss() noexcept
{
    return this->tss_;
}

const TaskStateSegment& TrapController::tss() const noexcept
{
    return this->tss_;
}

void TrapController::load_task_state_segment(TaskStateSegment table) noexcept
{
    this->tss_ = std::move(table);
}

InterruptDescriptorTable& TrapController::idt() noexcept
{
    return this->idt_;
}

const InterruptDescriptorTable& TrapController::idt() const noexcept
{
    return this->idt_;
}

void TrapController::load_idt(InterruptDescriptorTable table) noexcept
{
    this->idt_ = std::move(table);
}

const SyscallDescriptor& TrapController::syscall_descriptor() const noexcept
{
    return this->syscall_descriptor_;
}

void TrapController::configure_syscall(SyscallDescriptor descriptor) noexcept
{
    this->syscall_descriptor_ = descriptor;
}

TrapFrame TrapController::raise_exception(
    CpuState& state,
    const InterruptVector vector,
    std::optional<Qword> error_code) const
{
    return this->dispatch_gate_trap(state, TrapKind::EXCEPTION, vector, state.rip(), std::move(error_code));
}

TrapFrame TrapController::raise_interrupt(CpuState& state, const InterruptVector vector) const
{
    return this->dispatch_gate_trap(state, TrapKind::INTERRUPT, vector, state.rip(), std::nullopt);
}

TrapFrame TrapController::raise_software_interrupt(
    CpuState& state,
    const InterruptVector vector,
    const InstructionPointer return_rip) const
{
    return this->dispatch_gate_trap(state, TrapKind::SOFTWARE_INTERRUPT, vector, return_rip, std::nullopt);
}

TrapFrame TrapController::enter_syscall(CpuState& state, const InstructionPointer return_rip) const
{
    if (!this->syscall_descriptor_.is_enabled())
    {
        throw std::logic_error{TRAP_CONTROLLER_SYSCALL_DISABLED_MESSAGE};
    }

    const Qword original_rflags = state.flags().raw_bits();
    const Qword original_stack_pointer = state.registers().read(RegisterId::RSP);
    const TrapFrame frame{
        TrapKind::SYSCALL,
        InterruptVector::syscall_compat(),
        state.rip(),
        return_rip,
        original_rflags,
        original_stack_pointer,
        state.privilege_level()};

    state.registers().write(RegisterId::RCX, return_rip);
    state.registers().write(RegisterId::R11, original_rflags);
    state.set_pending_trap(frame);

    if (this->tss_.has_privilege_stack(PrivilegeLevel::RING0))
    {
        state.registers().write(RegisterId::RSP, this->tss_.privilege_stack(PrivilegeLevel::RING0));
    }

    state.set_privilege_level(PrivilegeLevel::RING0);
    state.flags().set_raw_bits(original_rflags & ~this->syscall_descriptor_.rflags_mask());
    state.set_rip(this->syscall_descriptor_.entry_rip());
    return frame;
}

void TrapController::return_from_trap(CpuState& state) const
{
    if (!state.has_pending_trap())
    {
        throw std::logic_error{TRAP_CONTROLLER_PENDING_TRAP_REQUIRED_MESSAGE};
    }

    const TrapFrame frame = state.pending_trap();
    this->restore_from_frame(state, frame);
    state.clear_pending_trap();
}

void TrapController::return_from_syscall(CpuState& state) const
{
    if (!state.has_pending_trap() || state.pending_trap().kind() != TrapKind::SYSCALL)
    {
        throw std::logic_error{TRAP_CONTROLLER_SYSCALL_TRAP_REQUIRED_MESSAGE};
    }

    const TrapFrame frame = state.pending_trap();
    state.set_rip(state.registers().read(RegisterId::RCX));
    state.flags().set_raw_bits(state.registers().read(RegisterId::R11));
    state.registers().write(RegisterId::RSP, frame.stack_pointer());
    state.set_privilege_level(frame.privilege_level());
    state.clear_pending_trap();
}

TrapFrame TrapController::dispatch_gate_trap(
    CpuState& state,
    const TrapKind kind,
    const InterruptVector vector,
    const InstructionPointer return_rip,
    std::optional<Qword> error_code) const
{
    const InterruptGate& gate = this->idt_.gate_at(vector);
    const TrapFrame frame{
        kind,
        vector,
        state.rip(),
        return_rip,
        state.flags().raw_bits(),
        state.registers().read(RegisterId::RSP),
        state.privilege_level(),
        std::move(error_code)};

    this->switch_to_gate_privilege(state, gate);
    state.set_pending_trap(frame);
    if (gate.type() == GateType::INTERRUPT_GATE)
    {
        state.flags().write(FlagId::IF, false);
    }
    state.set_rip(gate.handler_rip());
    return frame;
}

void TrapController::switch_to_gate_privilege(CpuState& state, const InterruptGate& gate) const
{
    const PrivilegeLevel target_level = gate.target_privilege_level();
    if (state.privilege_level() == target_level)
    {
        return;
    }

    if (is_more_privileged(target_level, state.privilege_level()))
    {
        if (!this->tss_.has_privilege_stack(target_level))
        {
            throw std::logic_error{TRAP_CONTROLLER_PRIVILEGE_STACK_REQUIRED_MESSAGE};
        }
        state.registers().write(RegisterId::RSP, this->tss_.privilege_stack(target_level));
    }

    state.set_privilege_level(target_level);
}

void TrapController::restore_from_frame(CpuState& state, const TrapFrame& frame) const
{
    state.set_rip(frame.return_rip());
    state.flags().set_raw_bits(frame.rflags());
    state.registers().write(RegisterId::RSP, frame.stack_pointer());
    state.set_privilege_level(frame.privilege_level());
}
}
