#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/opcode.hpp>

namespace
{
constexpr const char* EXECUTOR_MAX_STEPS_EXCEEDED_MESSAGE = "executor max steps exceeded before halt";
constexpr const char* EXECUTOR_READ_NONE_OPERAND_MESSAGE = "executor cannot read none operand";
constexpr const char* EXECUTOR_MEMORY_BUS_REQUIRED_MESSAGE = "executor memory operand requires a memory bus";
constexpr const char* EXECUTOR_REGISTER_DESTINATION_REQUIRED_MESSAGE =
    "executor destination operand must be a register or memory";
constexpr const char* EXECUTOR_TWO_MEMORY_OPERANDS_MESSAGE =
    "executor binary instruction cannot use two memory operands";
constexpr const char* EXECUTOR_JUMP_TARGET_OUT_OF_RANGE_MESSAGE = "executor jump target is out of program range";
constexpr const char* EXECUTOR_INVALID_OPCODE_MESSAGE = "executor received invalid opcode sentinel";
constexpr mnos::cpu::UQWORD64 EXECUTOR_ONE_BIT = mnos::cpu::UQWORD64{1};
constexpr mnos::cpu::UQWORD64 EXECUTOR_QWORD_SIGN_MASK =
    EXECUTOR_ONE_BIT << (mnos::cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});

[[nodiscard]] bool has_qword_sign_bit(const mnos::cpu::UQWORD64 value) noexcept
{
    return (value & EXECUTOR_QWORD_SIGN_MASK) != mnos::cpu::UQWORD64{0};
}

class ArithmeticFlagUpdater
{
public:
    static void update_add(
        mnos::cpu::Rflags& flags,
        const mnos::cpu::UQWORD64 left,
        const mnos::cpu::UQWORD64 right,
        const mnos::cpu::UQWORD64 result) noexcept
    {
        flags.update_zero_sign_from_qword(result);
        flags.write(mnos::cpu::FlagId::CF, result < left);
        flags.write(
            mnos::cpu::FlagId::OF,
            has_qword_sign_bit(left) == has_qword_sign_bit(right) &&
                has_qword_sign_bit(result) != has_qword_sign_bit(left));
    }

    static void update_sub(
        mnos::cpu::Rflags& flags,
        const mnos::cpu::UQWORD64 left,
        const mnos::cpu::UQWORD64 right,
        const mnos::cpu::UQWORD64 result) noexcept
    {
        flags.update_zero_sign_from_qword(result);
        flags.write(mnos::cpu::FlagId::CF, left < right);
        flags.write(
            mnos::cpu::FlagId::OF,
            has_qword_sign_bit(left) != has_qword_sign_bit(right) &&
                has_qword_sign_bit(result) != has_qword_sign_bit(left));
    }
};
}

namespace mnos::cpu
{
UQWORD64 Executor::cycle_count() const noexcept
{
    return this->cycle_count_;
}

void Executor::reset() noexcept
{
    this->cycle_count_ = UQWORD64{0};
}

StepResult Executor::step(CpuState& state, const Program& program, ExecutionTrace* const trace)
{
    return this->step_with_memory(state, program, nullptr, trace);
}

StepResult Executor::step(
    CpuState& state,
    const Program& program,
    MemoryBus& memory_bus,
    ExecutionTrace* const trace)
{
    return this->step_with_memory(state, program, &memory_bus, trace);
}

std::size_t Executor::run(
    CpuState& state,
    const Program& program,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    return this->run_with_memory(state, program, nullptr, max_steps, trace);
}

std::size_t Executor::run(
    CpuState& state,
    const Program& program,
    MemoryBus& memory_bus,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    return this->run_with_memory(state, program, &memory_bus, max_steps, trace);
}

StepResult Executor::step_with_memory(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    ExecutionTrace* const trace)
{
    if (state.is_halted())
    {
        return StepResult::HALTED;
    }

    const RIP64 rip_before = state.rip();
    const Instruction& instruction = program.instruction_at(rip_before);
    this->execute_instruction(state, program, memory_bus, instruction);
    ++this->cycle_count_;

    if (trace != nullptr)
    {
        trace->push_back(ExecutionTraceEntry{
            this->cycle_count_,
            rip_before,
            state.rip(),
            instruction.opcode(),
            state.is_halted()});
    }

    if (state.is_halted())
    {
        return StepResult::HALTED;
    }
    return StepResult::EXECUTED;
}

std::size_t Executor::run_with_memory(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    std::size_t executed_steps = 0;
    while (!state.is_halted() && executed_steps < max_steps)
    {
        static_cast<void>(this->step_with_memory(state, program, memory_bus, trace));
        ++executed_steps;
    }

    if (!state.is_halted())
    {
        throw std::runtime_error{EXECUTOR_MAX_STEPS_EXCEEDED_MESSAGE};
    }

    return executed_steps;
}

void Executor::execute_instruction(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    const Instruction& instruction)
{
    switch (instruction.opcode())
    {
    case Opcode::MOV:
        this->execute_mov(state, memory_bus, instruction);
        return;
    case Opcode::ADD:
        this->execute_add(state, memory_bus, instruction);
        return;
    case Opcode::SUB:
        this->execute_sub(state, memory_bus, instruction);
        return;
    case Opcode::CMP:
        this->execute_cmp(state, memory_bus, instruction);
        return;
    case Opcode::JMP:
        this->execute_jmp(state, program, memory_bus, instruction);
        return;
    case Opcode::JE:
        this->execute_je(state, program, memory_bus, instruction);
        return;
    case Opcode::JNE:
        this->execute_jne(state, program, memory_bus, instruction);
        return;
    case Opcode::HALT:
        this->execute_halt(state);
        return;
    case Opcode::COUNT:
        throw std::logic_error{EXECUTOR_INVALID_OPCODE_MESSAGE};
    }
}

void Executor::execute_mov(CpuState& state, MemoryBus* const memory_bus, const Instruction& instruction)
{
    this->require_at_most_one_memory_operand(instruction);
    this->write_operand(
        state,
        memory_bus,
        instruction.first_operand(),
        this->read_operand(state, memory_bus, instruction.second_operand()));
    state.advance_rip();
}

void Executor::execute_add(CpuState& state, MemoryBus* const memory_bus, const Instruction& instruction)
{
    this->require_at_most_one_memory_operand(instruction);
    const UQWORD64 left = this->read_operand(state, memory_bus, instruction.first_operand());
    const UQWORD64 right = this->read_operand(state, memory_bus, instruction.second_operand());
    const UQWORD64 result = left + right;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_add(state.flags(), left, right, result);
    state.advance_rip();
}

void Executor::execute_sub(CpuState& state, MemoryBus* const memory_bus, const Instruction& instruction)
{
    this->require_at_most_one_memory_operand(instruction);
    const UQWORD64 left = this->read_operand(state, memory_bus, instruction.first_operand());
    const UQWORD64 right = this->read_operand(state, memory_bus, instruction.second_operand());
    const UQWORD64 result = left - right;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_sub(state.flags(), left, right, result);
    state.advance_rip();
}

void Executor::execute_cmp(CpuState& state, MemoryBus* const memory_bus, const Instruction& instruction)
{
    this->require_at_most_one_memory_operand(instruction);
    const UQWORD64 left = this->read_operand(state, memory_bus, instruction.first_operand());
    const UQWORD64 right = this->read_operand(state, memory_bus, instruction.second_operand());
    const UQWORD64 result = left - right;
    ArithmeticFlagUpdater::update_sub(state.flags(), left, right, result);
    state.advance_rip();
}

void Executor::execute_jmp(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    const Instruction& instruction)
{
    this->jump_to(state, program, this->read_operand(state, memory_bus, instruction.first_operand()));
}

void Executor::execute_je(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    const Instruction& instruction)
{
    if (state.flags().read(FlagId::ZF))
    {
        this->jump_to(state, program, this->read_operand(state, memory_bus, instruction.first_operand()));
        return;
    }
    state.advance_rip();
}

void Executor::execute_jne(
    CpuState& state,
    const Program& program,
    MemoryBus* const memory_bus,
    const Instruction& instruction)
{
    if (!state.flags().read(FlagId::ZF))
    {
        this->jump_to(state, program, this->read_operand(state, memory_bus, instruction.first_operand()));
        return;
    }
    state.advance_rip();
}

void Executor::execute_halt(CpuState& state) const noexcept
{
    state.advance_rip();
    state.halt();
}

UQWORD64 Executor::read_operand(const CpuState& state, MemoryBus* const memory_bus, const Operand& operand) const
{
    if (operand.is_register())
    {
        return state.registers().read(operand.register_id());
    }

    if (operand.is_immediate())
    {
        return static_cast<UQWORD64>(operand.immediate_value());
    }

    if (operand.is_memory())
    {
        return this->read_memory_operand(state, memory_bus, operand);
    }

    throw std::logic_error{EXECUTOR_READ_NONE_OPERAND_MESSAGE};
}

void Executor::write_operand(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Operand& operand,
    const UQWORD64 value) const
{
    if (operand.is_register())
    {
        state.registers().write(operand.register_id(), value);
        return;
    }

    if (operand.is_memory())
    {
        this->write_memory_operand(state, memory_bus, operand, value);
        return;
    }

    throw std::logic_error{EXECUTOR_REGISTER_DESTINATION_REQUIRED_MESSAGE};
}

UQWORD64 Executor::read_memory_operand(
    const CpuState& state,
    MemoryBus* const memory_bus,
    const Operand& operand) const
{
    return this->require_memory_bus(memory_bus).read(
        this->calculate_effective_address(state, operand),
        operand.memory_data_size());
}

void Executor::write_memory_operand(
    const CpuState& state,
    MemoryBus* const memory_bus,
    const Operand& operand,
    const UQWORD64 value) const
{
    this->require_memory_bus(memory_bus).write(
        this->calculate_effective_address(state, operand),
        operand.memory_data_size(),
        value);
}

MemoryBus& Executor::require_memory_bus(MemoryBus* const memory_bus) const
{
    if (memory_bus == nullptr)
    {
        throw std::logic_error{EXECUTOR_MEMORY_BUS_REQUIRED_MESSAGE};
    }

    return *memory_bus;
}

ADDRESS64 Executor::calculate_effective_address(const CpuState& state, const Operand& operand) const
{
    const UQWORD64 base_address = state.registers().read(operand.memory_base_register());
    const UQWORD64 displacement = static_cast<UQWORD64>(operand.memory_displacement());

    // x86-64 effective address addition wraps in the address width before memory range checks.
    return base_address + displacement;
}

void Executor::require_at_most_one_memory_operand(const Instruction& instruction) const
{
    if (instruction.first_operand().is_memory() && instruction.second_operand().is_memory())
    {
        throw std::logic_error{EXECUTOR_TWO_MEMORY_OPERANDS_MESSAGE};
    }
}

void Executor::jump_to(CpuState& state, const Program& program, const UQWORD64 target) const
{
    if (!program.contains_rip(target))
    {
        throw std::out_of_range{EXECUTOR_JUMP_TARGET_OUT_OF_RANGE_MESSAGE};
    }
    state.set_rip(target);
}
}
