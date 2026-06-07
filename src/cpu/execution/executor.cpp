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
constexpr mnos::cpu::Qword EXECUTOR_ONE_BIT = mnos::cpu::Qword{1};
constexpr mnos::cpu::Qword EXECUTOR_QWORD_SIGN_MASK =
    EXECUTOR_ONE_BIT << (mnos::cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});

[[nodiscard]] bool has_qword_sign_bit(const mnos::cpu::Qword value) noexcept
{
    return (value & EXECUTOR_QWORD_SIGN_MASK) != mnos::cpu::Qword{0};
}

class ArithmeticFlagUpdater
{
public:
    static void update_add(
        mnos::cpu::Rflags& flags,
        const mnos::cpu::Qword left,
        const mnos::cpu::Qword right,
        const mnos::cpu::Qword result) noexcept
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
        const mnos::cpu::Qword left,
        const mnos::cpu::Qword right,
        const mnos::cpu::Qword result) noexcept
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
CycleCount Executor::cycle_count() const noexcept
{
    return this->cycle_count_;
}

void Executor::reset() noexcept
{
    this->cycle_count_ = CycleCount{0};
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

StepResult Executor::step(CpuState& state, const ExecutableImage& image, ExecutionTrace* const trace)
{
    return this->step_image_with_memory(state, image, nullptr, trace);
}

StepResult Executor::step(
    CpuState& state,
    const ExecutableImage& image,
    MemoryBus& memory_bus,
    ExecutionTrace* const trace)
{
    return this->step_image_with_memory(state, image, &memory_bus, trace);
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

std::size_t Executor::run(
    CpuState& state,
    const ExecutableImage& image,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    return this->run_image_with_memory(state, image, nullptr, max_steps, trace);
}

std::size_t Executor::run(
    CpuState& state,
    const ExecutableImage& image,
    MemoryBus& memory_bus,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    return this->run_image_with_memory(state, image, &memory_bus, max_steps, trace);
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

    const InstructionPointer rip_before = state.rip();
    const Instruction& instruction = program.instruction_at(rip_before);
    const ExecutionContext context{
        &program,
        nullptr,
        rip_before + CPU_STATE_NEXT_INSTRUCTION_OFFSET};
    this->execute_instruction(state, memory_bus, instruction, context);
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

StepResult Executor::step_image_with_memory(
    CpuState& state,
    const ExecutableImage& image,
    MemoryBus* const memory_bus,
    ExecutionTrace* const trace)
{
    if (state.is_halted())
    {
        return StepResult::HALTED;
    }

    const InstructionPointer rip_before = state.rip();
    const DecodedInstruction decoded_instruction = this->decoder_.decode(image, rip_before);
    const ExecutionContext context{nullptr, &image, decoded_instruction.next_rip};
    this->execute_instruction(state, memory_bus, decoded_instruction.instruction, context);
    ++this->cycle_count_;

    if (trace != nullptr)
    {
        trace->push_back(ExecutionTraceEntry{
            this->cycle_count_,
            decoded_instruction.start_rip,
            state.rip(),
            decoded_instruction.instruction.opcode(),
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

std::size_t Executor::run_image_with_memory(
    CpuState& state,
    const ExecutableImage& image,
    MemoryBus* const memory_bus,
    const std::size_t max_steps,
    ExecutionTrace* const trace)
{
    std::size_t executed_steps = 0;
    while (!state.is_halted() && executed_steps < max_steps)
    {
        static_cast<void>(this->step_image_with_memory(state, image, memory_bus, trace));
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
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    switch (instruction.opcode())
    {
    case Opcode::MOV:
        this->execute_mov(state, memory_bus, instruction, context);
        return;
    case Opcode::ADD:
        this->execute_add(state, memory_bus, instruction, context);
        return;
    case Opcode::SUB:
        this->execute_sub(state, memory_bus, instruction, context);
        return;
    case Opcode::CMP:
        this->execute_cmp(state, memory_bus, instruction, context);
        return;
    case Opcode::JMP:
        this->execute_jmp(state, memory_bus, instruction, context);
        return;
    case Opcode::JE:
        this->execute_je(state, memory_bus, instruction, context);
        return;
    case Opcode::JNE:
        this->execute_jne(state, memory_bus, instruction, context);
        return;
    case Opcode::HLT:
        this->execute_hlt(state, context);
        return;
    case Opcode::COUNT:
        throw std::logic_error{EXECUTOR_INVALID_OPCODE_MESSAGE};
    }
}

void Executor::execute_mov(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    this->write_operand(
        state,
        memory_bus,
        instruction.first_operand(),
        this->read_operand(state, memory_bus, instruction.second_operand()));
    this->set_next_rip(state, context);
}

void Executor::execute_add(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword left = this->read_operand(state, memory_bus, instruction.first_operand());
    const Qword right = this->read_operand(state, memory_bus, instruction.second_operand());
    const Qword result = left + right;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_add(state.flags(), left, right, result);
    this->set_next_rip(state, context);
}

void Executor::execute_sub(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword left = this->read_operand(state, memory_bus, instruction.first_operand());
    const Qword right = this->read_operand(state, memory_bus, instruction.second_operand());
    const Qword result = left - right;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_sub(state.flags(), left, right, result);
    this->set_next_rip(state, context);
}

void Executor::execute_cmp(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword left = this->read_operand(state, memory_bus, instruction.first_operand());
    const Qword right = this->read_operand(state, memory_bus, instruction.second_operand());
    const Qword result = left - right;
    ArithmeticFlagUpdater::update_sub(state.flags(), left, right, result);
    this->set_next_rip(state, context);
}

void Executor::execute_jmp(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->jump_to(state, context, this->read_operand(state, memory_bus, instruction.first_operand()));
}

void Executor::execute_je(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    if (state.flags().read(FlagId::ZF))
    {
        this->jump_to(state, context, this->read_operand(state, memory_bus, instruction.first_operand()));
        return;
    }
    this->set_next_rip(state, context);
}

void Executor::execute_jne(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    if (!state.flags().read(FlagId::ZF))
    {
        this->jump_to(state, context, this->read_operand(state, memory_bus, instruction.first_operand()));
        return;
    }
    this->set_next_rip(state, context);
}

void Executor::execute_hlt(CpuState& state, const ExecutionContext& context) const noexcept
{
    this->set_next_rip(state, context);
    state.halt();
}

Qword Executor::read_operand(const CpuState& state, MemoryBus* const memory_bus, const Operand& operand) const
{
    if (operand.is_register())
    {
        return state.registers().read(operand.register_id());
    }

    if (operand.is_immediate())
    {
        return static_cast<Qword>(operand.immediate_value());
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
    const Qword value) const
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

Qword Executor::read_memory_operand(
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
    const Qword value) const
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

Address64 Executor::calculate_effective_address(const CpuState& state, const Operand& operand) const
{
    if (operand.memory_has_absolute_address())
    {
        return operand.memory_absolute_address();
    }

    Qword effective_address = Qword{0};
    if (operand.memory_has_base_register())
    {
        effective_address += state.registers().read(operand.memory_base_register());
    }

    if (operand.memory_has_index_register())
    {
        effective_address += state.registers().read(operand.memory_index_register()) * operand.memory_scale();
    }

    const Qword displacement = static_cast<Qword>(operand.memory_displacement());

    // x86-64 effective address addition wraps in the address width before memory range checks.
    return effective_address + displacement;
}

void Executor::require_at_most_one_memory_operand(const Instruction& instruction) const
{
    if (instruction.first_operand().is_memory() && instruction.second_operand().is_memory())
    {
        throw std::logic_error{EXECUTOR_TWO_MEMORY_OPERANDS_MESSAGE};
    }
}

void Executor::set_next_rip(CpuState& state, const ExecutionContext& context) const noexcept
{
    state.set_rip(context.next_rip);
}

void Executor::jump_to(CpuState& state, const ExecutionContext& context, const InstructionPointer target) const
{
    if (context.program != nullptr && !context.program->contains_rip(target))
    {
        throw std::out_of_range{EXECUTOR_JUMP_TARGET_OUT_OF_RANGE_MESSAGE};
    }

    if (context.image != nullptr && !context.image->contains_rip(target))
    {
        throw std::out_of_range{EXECUTOR_JUMP_TARGET_OUT_OF_RANGE_MESSAGE};
    }

    state.set_rip(target);
}
}
