#pragma once

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/register/bank.hpp>

namespace mnos::cpu
{
inline constexpr RIP64 CPU_STATE_INITIAL_RIP = RIP64{0};
inline constexpr RIP64 CPU_STATE_NEXT_INSTRUCTION_OFFSET = RIP64{1};

class CpuState
{
public:
    [[nodiscard]] RegisterBank& registers() noexcept;
    [[nodiscard]] const RegisterBank& registers() const noexcept;

    [[nodiscard]] Rflags& flags() noexcept;
    [[nodiscard]] const Rflags& flags() const noexcept;

    [[nodiscard]] RIP64 rip() const noexcept;
    void set_rip(RIP64 value) noexcept;
    void advance_rip() noexcept;

    [[nodiscard]] bool is_halted() const noexcept;
    void halt() noexcept;
    void resume() noexcept;
    void reset() noexcept;

private:
    RegisterBank registers_;
    Rflags flags_;
    RIP64 rip_ = CPU_STATE_INITIAL_RIP;
    bool halted_ = false;
};
}
