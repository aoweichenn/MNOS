#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/decode/decoder.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/perf/performance_model.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

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
    [[nodiscard]] CycleCount cycle_count() const noexcept;
    void reset() noexcept;
    void attach_trap_controller(system::TrapController& trap_controller) noexcept;
    void detach_trap_controller() noexcept;
    [[nodiscard]] bool has_trap_controller() const noexcept;
    [[nodiscard]] memory::MemoryManagementUnit& mmu() noexcept;
    [[nodiscard]] const memory::MemoryManagementUnit& mmu() const noexcept;
    void enable_stage8_performance_model(perf::Stage8PerformanceConfig config = perf::Stage8PerformanceConfig{});
    void disable_stage8_performance_model() noexcept;
    [[nodiscard]] bool has_stage8_performance_model() const noexcept;
    [[nodiscard]] perf::Stage8PerformanceModel& stage8_performance_model();
    [[nodiscard]] const perf::Stage8PerformanceModel& stage8_performance_model() const;

    [[nodiscard]] StepResult step(CpuState& state, const Program& program, ExecutionTrace* trace = nullptr);
    [[nodiscard]] StepResult step(
        CpuState& state,
        const Program& program,
        MemoryBus& memory_bus,
        ExecutionTrace* trace = nullptr);
    [[nodiscard]] StepResult step(CpuState& state, const ExecutableImage& image, ExecutionTrace* trace = nullptr);
    [[nodiscard]] StepResult step(
        CpuState& state,
        const ExecutableImage& image,
        MemoryBus& memory_bus,
        ExecutionTrace* trace = nullptr);
    [[nodiscard]] std::size_t run(
        CpuState& state,
        const Program& program,
        std::size_t max_steps = EXECUTOR_DEFAULT_MAX_STEPS,
        ExecutionTrace* trace = nullptr);
    [[nodiscard]] std::size_t run(
        CpuState& state,
        const Program& program,
        MemoryBus& memory_bus,
        std::size_t max_steps = EXECUTOR_DEFAULT_MAX_STEPS,
        ExecutionTrace* trace = nullptr);
    [[nodiscard]] std::size_t run(
        CpuState& state,
        const ExecutableImage& image,
        std::size_t max_steps = EXECUTOR_DEFAULT_MAX_STEPS,
        ExecutionTrace* trace = nullptr);
    [[nodiscard]] std::size_t run(
        CpuState& state,
        const ExecutableImage& image,
        MemoryBus& memory_bus,
        std::size_t max_steps = EXECUTOR_DEFAULT_MAX_STEPS,
        ExecutionTrace* trace = nullptr);

private:
    struct ExecutionContext
    {
        const Program* program = nullptr;
        const ExecutableImage* image = nullptr;
        InstructionPointer next_rip = InstructionPointer{0};
    };

    [[nodiscard]] StepResult step_with_memory(
        CpuState& state,
        const Program& program,
        MemoryBus* memory_bus,
        ExecutionTrace* trace);
    [[nodiscard]] StepResult step_image_with_memory(
        CpuState& state,
        const ExecutableImage& image,
        MemoryBus* memory_bus,
        ExecutionTrace* trace);
    [[nodiscard]] std::size_t run_with_memory(
        CpuState& state,
        const Program& program,
        MemoryBus* memory_bus,
        std::size_t max_steps,
        ExecutionTrace* trace);
    [[nodiscard]] std::size_t run_image_with_memory(
        CpuState& state,
        const ExecutableImage& image,
        MemoryBus* memory_bus,
        std::size_t max_steps,
        ExecutionTrace* trace);

    void execute_instruction(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_mov(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_movsx(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_movzx(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_lea(
        CpuState& state,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_add(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_sub(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_cmp(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_inc(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_dec(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_and(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_or(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_xor(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_test(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_cmpxchg(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_xadd(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_mfence(CpuState& state, const ExecutionContext& context) const noexcept;
    void execute_invlpg(CpuState& state, const Instruction& instruction, const ExecutionContext& context);
    void execute_push(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_pop(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_call(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_ret(CpuState& state, MemoryBus* memory_bus, const ExecutionContext& context);
    void execute_jmp(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_je(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_jne(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_jcc(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_setcc(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_cmovcc(
        CpuState& state,
        MemoryBus* memory_bus,
        const Instruction& instruction,
        const ExecutionContext& context);
    void execute_int(CpuState& state, const Instruction& instruction, const ExecutionContext& context) const;
    void execute_syscall(CpuState& state, const ExecutionContext& context) const;
    void execute_sysret(CpuState& state) const;
    void execute_iret(CpuState& state) const;
    void execute_hlt(CpuState& state, const ExecutionContext& context) const noexcept;

    [[nodiscard]] Qword read_operand(CpuState& state, MemoryBus* memory_bus, const Operand& operand);
    void write_operand(CpuState& state, MemoryBus* memory_bus, const Operand& operand, Qword value);
    [[nodiscard]] Qword read_register_operand(const CpuState& state, const Operand& operand) const;
    void write_register_operand(CpuState& state, const Operand& operand, Qword value) const;
    [[nodiscard]] Qword read_memory_operand(
        CpuState& state,
        MemoryBus* memory_bus,
        const Operand& operand);
    void write_memory_operand(CpuState& state, MemoryBus* memory_bus, const Operand& operand, Qword value);
    void push_qword(CpuState& state, MemoryBus* memory_bus, Qword value);
    [[nodiscard]] Qword pop_qword(CpuState& state, MemoryBus* memory_bus);
    [[nodiscard]] MemoryBus& require_memory_bus(MemoryBus* memory_bus) const;
    [[nodiscard]] Address64 calculate_effective_address(const CpuState& state, const Operand& operand) const;
    [[nodiscard]] bool evaluate_condition(const CpuState& state, ConditionCode condition) const;
    void require_at_most_one_memory_operand(const Instruction& instruction) const;
    void require_register_operand(const Operand& operand) const;
    void require_memory_operand(const Operand& operand) const;
    void require_locked_memory_destination(const Instruction& instruction) const;
    [[nodiscard]] system::TrapController& require_trap_controller() const;
    void check_instruction_fetch(
        CpuState& state,
        MemoryBus* memory_bus,
        InstructionPointer start_rip,
        InstructionPointer next_rip);
    void record_stage8_instruction_fetch(InstructionPointer start_rip, InstructionPointer next_rip);
    void record_stage8_retired_instruction(
        const Instruction& instruction,
        InstructionPointer fallthrough_rip,
        InstructionPointer actual_rip) noexcept;
    void record_stage8_exception_flush() noexcept;
    [[nodiscard]] bool is_control_flow_opcode(Opcode opcode) const noexcept;
    void handle_page_fault(CpuState& state, const memory::PageFault& fault) const;
    void set_next_rip(CpuState& state, const ExecutionContext& context) const noexcept;
    void jump_to(CpuState& state, const ExecutionContext& context, InstructionPointer target) const;

    Decoder decoder_;
    memory::MemoryManagementUnit mmu_;
    std::optional<perf::Stage8PerformanceModel> stage8_performance_model_;
    system::TrapController* trap_controller_ = nullptr;
    CycleCount cycle_count_ = CycleCount{0};
};
}
