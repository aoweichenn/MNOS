#pragma once

#include <optional>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/register/bank.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_frame.hpp>

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

    [[nodiscard]] memory::PagingState& paging() noexcept;
    [[nodiscard]] const memory::PagingState& paging() const noexcept;

    [[nodiscard]] InstructionPointer rip() const noexcept;
    void set_rip(InstructionPointer value) noexcept;
    void advance_rip() noexcept;

    [[nodiscard]] bool is_halted() const noexcept;
    void halt() noexcept;
    void resume() noexcept;
    void reset() noexcept;

    [[nodiscard]] system::PrivilegeLevel privilege_level() const noexcept;
    void set_privilege_level(system::PrivilegeLevel level);

    [[nodiscard]] bool has_pending_trap() const noexcept;
    [[nodiscard]] const system::TrapFrame& pending_trap() const;
    void set_pending_trap(system::TrapFrame frame);
    void clear_pending_trap() noexcept;

private:
    RegisterBank registers_;
    Rflags flags_;
    memory::PagingState paging_;
    InstructionPointer rip_ = CPU_STATE_INITIAL_RIP;
    system::PrivilegeLevel privilege_level_ = system::PrivilegeLevel::RING0;
    std::optional<system::TrapFrame> pending_trap_;
    bool halted_ = false;
};
}
