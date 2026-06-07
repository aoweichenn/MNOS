#include <mnos/cpu/execution/cpu_state.hpp>

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

RIP64 CpuState::rip() const noexcept
{
    return this->rip_;
}

void CpuState::set_rip(const RIP64 value) noexcept
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
    this->halted_ = false;
}
}
