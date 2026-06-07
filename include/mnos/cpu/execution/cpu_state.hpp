#pragma once

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/register/bank.hpp>

namespace mnos::cpu
{
inline constexpr InstructionPointer CPU_STATE_INITIAL_RIP = InstructionPointer{0};
inline constexpr InstructionPointer CPU_STATE_NEXT_INSTRUCTION_OFFSET = InstructionPointer{1};

class CpuState
{
public:
    [[nodiscard]] RegisterBank& registers() noexcept;
    [[nodiscard]] const RegisterBank& registers() const noexcept;

    [[nodiscard]] Rflags& flags() noexcept;
    [[nodiscard]] const Rflags& flags() const noexcept;

    [[nodiscard]] InstructionPointer rip() const noexcept;
    void set_rip(InstructionPointer value) noexcept;
    void advance_rip() noexcept;

    [[nodiscard]] bool is_halted() const noexcept;
    void halt() noexcept;
    void resume() noexcept;
    void reset() noexcept;

private:
    RegisterBank registers_;
    Rflags flags_;
    InstructionPointer rip_ = CPU_STATE_INITIAL_RIP;
    bool halted_ = false;
};
}
