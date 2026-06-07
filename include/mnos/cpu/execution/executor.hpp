#pragma once

#include <cstddef>
#include <cstdint>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>

namespace mnos::cpu
{
inline constexpr std::size_t EXECUTOR_DEFAULT_MAX_STEPS = 4096;

enum class StepResult : std::uint8_t
{
    EXECUTED,
    HALTED,
    COUNT
};

class Executor
{
public:
    [[nodiscard]] UQWORD64 cycle_count() const noexcept;
    void reset() noexcept;

    [[nodiscard]] StepResult step(CpuState& state, const Program& program, ExecutionTrace* trace = nullptr);
    [[nodiscard]] std::size_t run(
        CpuState& state,
        const Program& program,
        std::size_t max_steps = EXECUTOR_DEFAULT_MAX_STEPS,
        ExecutionTrace* trace = nullptr);

private:
    void execute_instruction(CpuState& state, const Program& program, const Instruction& instruction);
    void execute_mov(CpuState& state, const Instruction& instruction);
    void execute_add(CpuState& state, const Instruction& instruction);
    void execute_sub(CpuState& state, const Instruction& instruction);
    void execute_cmp(CpuState& state, const Instruction& instruction);
    void execute_jmp(CpuState& state, const Program& program, const Instruction& instruction);
    void execute_je(CpuState& state, const Program& program, const Instruction& instruction);
    void execute_jne(CpuState& state, const Program& program, const Instruction& instruction);
    void execute_halt(CpuState& state) const noexcept;

    [[nodiscard]] UQWORD64 read_operand(const CpuState& state, const Operand& operand) const;
    void write_register_operand(CpuState& state, const Operand& operand, UQWORD64 value) const;
    void jump_to(CpuState& state, const Program& program, UQWORD64 target) const;

    UQWORD64 cycle_count_ = UQWORD64{0};
};
}
