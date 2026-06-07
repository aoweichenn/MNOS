#include <stdexcept>
#include <utility>

#include <mnos/cpu/execution/cpu_state.hpp>

namespace
{
constexpr const char* CPU_STATE_INVALID_PRIVILEGE_LEVEL_MESSAGE = "cpu state privilege level is invalid";
constexpr const char* CPU_STATE_PENDING_TRAP_NOT_PRESENT_MESSAGE = "cpu state has no pending trap";
}

namespace mnos::cpu
{
RegisterBank& CpuState::registers() noexcept
{
    return this->registers_;
}

const RegisterBank& CpuState::registers() const noexcept
{
    return this->registers_;
}

Rflags& CpuState::flags() noexcept
{
    return this->flags_;
}

const Rflags& CpuState::flags() const noexcept
{
    return this->flags_;
}

InstructionPointer CpuState::rip() const noexcept
{
    return this->rip_;
}

void CpuState::set_rip(const InstructionPointer value) noexcept
{
    this->rip_ = value;
}

void CpuState::advance_rip() noexcept
{
    this->rip_ += CPU_STATE_NEXT_INSTRUCTION_OFFSET;
}

bool CpuState::is_halted() const noexcept
{
    return this->halted_;
}

void CpuState::halt() noexcept
{
    this->halted_ = true;
}

void CpuState::resume() noexcept
{
    this->halted_ = false;
}

void CpuState::reset() noexcept
{
    this->registers_ = RegisterBank{};
    this->flags_ = Rflags{};
    this->rip_ = CPU_STATE_INITIAL_RIP;
    this->privilege_level_ = system::PrivilegeLevel::RING0;
    this->pending_trap_.reset();
    this->halted_ = false;
}

system::PrivilegeLevel CpuState::privilege_level() const noexcept
{
    return this->privilege_level_;
}

void CpuState::set_privilege_level(const system::PrivilegeLevel level)
{
    if (!system::is_privilege_level_valid(level))
    {
        throw std::out_of_range{CPU_STATE_INVALID_PRIVILEGE_LEVEL_MESSAGE};
    }
    this->privilege_level_ = level;
}

bool CpuState::has_pending_trap() const noexcept
{
    return this->pending_trap_.has_value();
}

const system::TrapFrame& CpuState::pending_trap() const
{
    if (!this->pending_trap_.has_value())
    {
        throw std::logic_error{CPU_STATE_PENDING_TRAP_NOT_PRESENT_MESSAGE};
    }
    return this->pending_trap_.value();
}

void CpuState::set_pending_trap(system::TrapFrame frame)
{
    this->pending_trap_ = std::move(frame);
}

void CpuState::clear_pending_trap() noexcept
{
    this->pending_trap_.reset();
}
}
