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
constexpr const char* EXECUTOR_REGISTER_OPERAND_REQUIRED_MESSAGE = "executor instruction requires a register operand";
constexpr const char* EXECUTOR_MEMORY_OPERAND_REQUIRED_MESSAGE = "executor instruction requires a memory operand";
constexpr const char* EXECUTOR_JUMP_TARGET_OUT_OF_RANGE_MESSAGE = "executor jump target is out of program range";
constexpr const char* EXECUTOR_INVALID_OPCODE_MESSAGE = "executor received invalid opcode sentinel";
constexpr const char* EXECUTOR_INVALID_CONDITION_CODE_MESSAGE = "executor received invalid condition code sentinel";
constexpr const char* EXECUTOR_INVALID_DATA_SIZE_MESSAGE = "executor data size is invalid";
constexpr const char* EXECUTOR_TRAP_CONTROLLER_REQUIRED_MESSAGE =
    "executor trap instruction requires an attached trap controller";
constexpr const char* EXECUTOR_INTERRUPT_VECTOR_OPERAND_REQUIRED_MESSAGE =
    "executor INT instruction requires an 8-bit vector immediate";
constexpr mnos::cpu::Qword EXECUTOR_ONE_BIT = mnos::cpu::Qword{1};
constexpr mnos::cpu::Qword EXECUTOR_LOW_BYTE_MASK = mnos::cpu::Qword{0xFF};
constexpr mnos::cpu::Qword EXECUTOR_WORD_MASK = mnos::cpu::Qword{0xFFFF};
constexpr mnos::cpu::Qword EXECUTOR_DWORD_MASK = mnos::cpu::Qword{0xFFFF'FFFF};
constexpr mnos::cpu::Qword EXECUTOR_SETCC_FALSE_VALUE = mnos::cpu::Qword{0};
constexpr mnos::cpu::Qword EXECUTOR_SETCC_TRUE_VALUE = mnos::cpu::Qword{1};
constexpr mnos::cpu::Qword EXECUTOR_STACK_SLOT_BYTES =
    static_cast<mnos::cpu::Qword>(mnos::cpu::DATA_SIZE_QWORD_BYTES);
constexpr mnos::cpu::Qword EXECUTOR_QWORD_SIGN_MASK =
    EXECUTOR_ONE_BIT << (mnos::cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});
constexpr mnos::cpu::SignedQword EXECUTOR_INTERRUPT_VECTOR_MIN_VALUE = mnos::cpu::SignedQword{0};
constexpr mnos::cpu::SignedQword EXECUTOR_INTERRUPT_VECTOR_MAX_VALUE =
    static_cast<mnos::cpu::SignedQword>(mnos::cpu::system::INTERRUPT_VECTOR_COUNT - std::size_t{1});

[[nodiscard]] bool has_qword_sign_bit(const mnos::cpu::Qword value) noexcept
{
    return (value & EXECUTOR_QWORD_SIGN_MASK) != mnos::cpu::Qword{0};
}

[[nodiscard]] mnos::cpu::Qword mask_for_data_size(const mnos::cpu::DataSize size)
{
    switch (size)
    {
    case mnos::cpu::DataSize::BYTE:
        return EXECUTOR_LOW_BYTE_MASK;
    case mnos::cpu::DataSize::WORD:
        return EXECUTOR_WORD_MASK;
    case mnos::cpu::DataSize::DWORD:
        return EXECUTOR_DWORD_MASK;
    case mnos::cpu::DataSize::QWORD:
        return ~mnos::cpu::Qword{0};
    case mnos::cpu::DataSize::COUNT:
        break;
    }

    throw std::logic_error{EXECUTOR_INVALID_DATA_SIZE_MESSAGE};
}

[[nodiscard]] std::size_t sign_bit_index_for_data_size(const mnos::cpu::DataSize size)
{
    return mnos::cpu::data_size_to_bits(size) - std::size_t{1};
}

[[nodiscard]] mnos::cpu::Qword sign_extend_value(
    const mnos::cpu::Qword value,
    const mnos::cpu::DataSize source_size)
{
    const mnos::cpu::Qword value_mask = mask_for_data_size(source_size);
    const mnos::cpu::Qword masked_value = value & value_mask;
    const mnos::cpu::Qword sign_mask = EXECUTOR_ONE_BIT << sign_bit_index_for_data_size(source_size);
    if ((masked_value & sign_mask) == mnos::cpu::Qword{0})
    {
        return masked_value;
    }

    return masked_value | ~value_mask;
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
        flags.update_zero_sign_parity_from_qword(result);
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
        flags.update_zero_sign_parity_from_qword(result);
        flags.write(mnos::cpu::FlagId::CF, left < right);
        flags.write(
            mnos::cpu::FlagId::OF,
            has_qword_sign_bit(left) != has_qword_sign_bit(right) &&
                has_qword_sign_bit(result) != has_qword_sign_bit(left));
    }

    static void update_inc(
        mnos::cpu::Rflags& flags,
        const mnos::cpu::Qword left,
        const mnos::cpu::Qword result) noexcept
    {
        flags.update_zero_sign_parity_from_qword(result);
        flags.write(mnos::cpu::FlagId::OF, !has_qword_sign_bit(left) && has_qword_sign_bit(result));
    }

    static void update_dec(
        mnos::cpu::Rflags& flags,
        const mnos::cpu::Qword left,
        const mnos::cpu::Qword result) noexcept
    {
        flags.update_zero_sign_parity_from_qword(result);
        flags.write(mnos::cpu::FlagId::OF, has_qword_sign_bit(left) && !has_qword_sign_bit(result));
    }
};

class LogicalFlagUpdater
{
public:
    static void update(mnos::cpu::Rflags& flags, const mnos::cpu::Qword result) noexcept
    {
        flags.update_zero_sign_parity_from_qword(result);
        flags.write(mnos::cpu::FlagId::CF, false);
        flags.write(mnos::cpu::FlagId::OF, false);
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

void Executor::attach_trap_controller(system::TrapController& trap_controller) noexcept
{
    this->trap_controller_ = &trap_controller;
}

void Executor::detach_trap_controller() noexcept
{
    this->trap_controller_ = nullptr;
}

bool Executor::has_trap_controller() const noexcept
{
    return this->trap_controller_ != nullptr;
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
            state.is_halted(),
            state.has_pending_trap()});
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
            state.is_halted(),
            state.has_pending_trap()});
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
    case Opcode::MOVSX:
        this->execute_movsx(state, memory_bus, instruction, context);
        return;
    case Opcode::MOVZX:
        this->execute_movzx(state, memory_bus, instruction, context);
        return;
    case Opcode::LEA:
        this->execute_lea(state, instruction, context);
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
    case Opcode::INC:
        this->execute_inc(state, memory_bus, instruction, context);
        return;
    case Opcode::DEC:
        this->execute_dec(state, memory_bus, instruction, context);
        return;
    case Opcode::AND:
        this->execute_and(state, memory_bus, instruction, context);
        return;
    case Opcode::OR:
        this->execute_or(state, memory_bus, instruction, context);
        return;
    case Opcode::XOR:
        this->execute_xor(state, memory_bus, instruction, context);
        return;
    case Opcode::TEST:
        this->execute_test(state, memory_bus, instruction, context);
        return;
    case Opcode::PUSH:
        this->execute_push(state, memory_bus, instruction, context);
        return;
    case Opcode::POP:
        this->execute_pop(state, memory_bus, instruction, context);
        return;
    case Opcode::CALL:
        this->execute_call(state, memory_bus, instruction, context);
        return;
    case Opcode::RET:
        this->execute_ret(state, memory_bus, context);
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
    case Opcode::JCC:
        this->execute_jcc(state, memory_bus, instruction, context);
        return;
    case Opcode::SETCC:
        this->execute_setcc(state, memory_bus, instruction, context);
        return;
    case Opcode::CMOVCC:
        this->execute_cmovcc(state, memory_bus, instruction, context);
        return;
    case Opcode::INT:
        this->execute_int(state, instruction, context);
        return;
    case Opcode::SYSCALL:
        this->execute_syscall(state, context);
        return;
    case Opcode::SYSRET:
        this->execute_sysret(state);
        return;
    case Opcode::IRET:
        this->execute_iret(state);
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

void Executor::execute_movsx(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_register_operand(instruction.first_operand());
    const Qword source = this->read_operand(state, memory_bus, instruction.second_operand());
    const DataSize source_size = instruction.second_operand().is_register()
        ? instruction.second_operand().register_data_size()
        : instruction.second_operand().memory_data_size();
    this->write_register_operand(state, instruction.first_operand(), sign_extend_value(source, source_size));
    this->set_next_rip(state, context);
}

void Executor::execute_movzx(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_register_operand(instruction.first_operand());
    this->write_register_operand(
        state,
        instruction.first_operand(),
        this->read_operand(state, memory_bus, instruction.second_operand()));
    this->set_next_rip(state, context);
}

void Executor::execute_lea(CpuState& state, const Instruction& instruction, const ExecutionContext& context)
{
    this->require_register_operand(instruction.first_operand());
    this->require_memory_operand(instruction.second_operand());
    this->write_register_operand(
        state,
        instruction.first_operand(),
        this->calculate_effective_address(state, instruction.second_operand()));
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

void Executor::execute_inc(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    const Qword left = this->read_operand(state, memory_bus, instruction.first_operand());
    const Qword result = left + EXECUTOR_ONE_BIT;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_inc(state.flags(), left, result);
    this->set_next_rip(state, context);
}

void Executor::execute_dec(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    const Qword left = this->read_operand(state, memory_bus, instruction.first_operand());
    const Qword result = left - EXECUTOR_ONE_BIT;
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    ArithmeticFlagUpdater::update_dec(state.flags(), left, result);
    this->set_next_rip(state, context);
}

void Executor::execute_and(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword result =
        this->read_operand(state, memory_bus, instruction.first_operand()) &
        this->read_operand(state, memory_bus, instruction.second_operand());
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    LogicalFlagUpdater::update(state.flags(), result);
    this->set_next_rip(state, context);
}

void Executor::execute_or(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword result =
        this->read_operand(state, memory_bus, instruction.first_operand()) |
        this->read_operand(state, memory_bus, instruction.second_operand());
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    LogicalFlagUpdater::update(state.flags(), result);
    this->set_next_rip(state, context);
}

void Executor::execute_xor(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword result =
        this->read_operand(state, memory_bus, instruction.first_operand()) ^
        this->read_operand(state, memory_bus, instruction.second_operand());
    this->write_operand(state, memory_bus, instruction.first_operand(), result);
    LogicalFlagUpdater::update(state.flags(), result);
    this->set_next_rip(state, context);
}

void Executor::execute_test(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_at_most_one_memory_operand(instruction);
    const Qword result =
        this->read_operand(state, memory_bus, instruction.first_operand()) &
        this->read_operand(state, memory_bus, instruction.second_operand());
    LogicalFlagUpdater::update(state.flags(), result);
    this->set_next_rip(state, context);
}

void Executor::execute_push(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->push_qword(state, memory_bus, this->read_operand(state, memory_bus, instruction.first_operand()));
    this->set_next_rip(state, context);
}

void Executor::execute_pop(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->write_operand(state, memory_bus, instruction.first_operand(), this->pop_qword(state, memory_bus));
    this->set_next_rip(state, context);
}

void Executor::execute_call(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    const Qword target = this->read_operand(state, memory_bus, instruction.first_operand());
    this->push_qword(state, memory_bus, context.next_rip);
    this->jump_to(state, context, target);
}

void Executor::execute_ret(CpuState& state, MemoryBus* const memory_bus, const ExecutionContext& context)
{
    this->jump_to(state, context, this->pop_qword(state, memory_bus));
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

void Executor::execute_jcc(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    if (this->evaluate_condition(state, instruction.condition_code()))
    {
        this->jump_to(state, context, this->read_operand(state, memory_bus, instruction.first_operand()));
        return;
    }
    this->set_next_rip(state, context);
}

void Executor::execute_setcc(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->write_operand(
        state,
        memory_bus,
        instruction.first_operand(),
        this->evaluate_condition(state, instruction.condition_code()) ? EXECUTOR_SETCC_TRUE_VALUE
                                                                      : EXECUTOR_SETCC_FALSE_VALUE);
    this->set_next_rip(state, context);
}

void Executor::execute_cmovcc(
    CpuState& state,
    MemoryBus* const memory_bus,
    const Instruction& instruction,
    const ExecutionContext& context)
{
    this->require_register_operand(instruction.first_operand());
    this->require_at_most_one_memory_operand(instruction);
    if (this->evaluate_condition(state, instruction.condition_code()))
    {
        this->write_register_operand(
            state,
            instruction.first_operand(),
            this->read_operand(state, memory_bus, instruction.second_operand()));
    }
    this->set_next_rip(state, context);
}

void Executor::execute_int(CpuState& state, const Instruction& instruction, const ExecutionContext& context) const
{
    if (!instruction.first_operand().is_immediate())
    {
        throw std::logic_error{EXECUTOR_INTERRUPT_VECTOR_OPERAND_REQUIRED_MESSAGE};
    }

    const SignedQword vector_value = instruction.first_operand().immediate_value();
    if (vector_value < EXECUTOR_INTERRUPT_VECTOR_MIN_VALUE || vector_value > EXECUTOR_INTERRUPT_VECTOR_MAX_VALUE)
    {
        throw std::logic_error{EXECUTOR_INTERRUPT_VECTOR_OPERAND_REQUIRED_MESSAGE};
    }

    static_cast<void>(this->require_trap_controller().raise_software_interrupt(
        state,
        system::InterruptVector{static_cast<system::InterruptVector::value_type>(vector_value)},
        context.next_rip));
}

void Executor::execute_syscall(CpuState& state, const ExecutionContext& context) const
{
    static_cast<void>(this->require_trap_controller().enter_syscall(state, context.next_rip));
}

void Executor::execute_sysret(CpuState& state) const
{
    this->require_trap_controller().return_from_syscall(state);
}

void Executor::execute_iret(CpuState& state) const
{
    this->require_trap_controller().return_from_trap(state);
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
        return this->read_register_operand(state, operand);
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
        this->write_register_operand(state, operand, value);
        return;
    }

    if (operand.is_memory())
    {
        this->write_memory_operand(state, memory_bus, operand, value);
        return;
    }

    throw std::logic_error{EXECUTOR_REGISTER_DESTINATION_REQUIRED_MESSAGE};
}

Qword Executor::read_register_operand(const CpuState& state, const Operand& operand) const
{
    return state.registers().read(operand.register_id()) & mask_for_data_size(operand.register_data_size());
}

void Executor::write_register_operand(CpuState& state, const Operand& operand, const Qword value) const
{
    const DataSize data_size = operand.register_data_size();
    const Qword value_mask = mask_for_data_size(data_size);
    if (data_size == DataSize::QWORD || data_size == DataSize::DWORD)
    {
        state.registers().write(operand.register_id(), value & value_mask);
        return;
    }

    const Qword preserved_bits = state.registers().read(operand.register_id()) & ~value_mask;
    state.registers().write(operand.register_id(), preserved_bits | (value & value_mask));
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

void Executor::push_qword(CpuState& state, MemoryBus* const memory_bus, const Qword value) const
{
    const Qword stack_pointer = state.registers().read(RegisterId::RSP) - EXECUTOR_STACK_SLOT_BYTES;
    state.registers().write(RegisterId::RSP, stack_pointer);
    this->require_memory_bus(memory_bus).write(stack_pointer, DataSize::QWORD, value);
}

Qword Executor::pop_qword(CpuState& state, MemoryBus* const memory_bus) const
{
    const Qword stack_pointer = state.registers().read(RegisterId::RSP);
    const Qword value = this->require_memory_bus(memory_bus).read(stack_pointer, DataSize::QWORD);
    state.registers().write(RegisterId::RSP, stack_pointer + EXECUTOR_STACK_SLOT_BYTES);
    return value;
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

bool Executor::evaluate_condition(const CpuState& state, const ConditionCode condition) const
{
    const bool carry = state.flags().read(FlagId::CF);
    const bool parity = state.flags().read(FlagId::PF);
    const bool zero = state.flags().read(FlagId::ZF);
    const bool sign = state.flags().read(FlagId::SF);
    const bool overflow = state.flags().read(FlagId::OF);

    switch (condition)
    {
    case ConditionCode::O:
        return overflow;
    case ConditionCode::NO:
        return !overflow;
    case ConditionCode::B:
        return carry;
    case ConditionCode::AE:
        return !carry;
    case ConditionCode::E:
        return zero;
    case ConditionCode::NE:
        return !zero;
    case ConditionCode::BE:
        return carry || zero;
    case ConditionCode::A:
        return !carry && !zero;
    case ConditionCode::S:
        return sign;
    case ConditionCode::NS:
        return !sign;
    case ConditionCode::P:
        return parity;
    case ConditionCode::NP:
        return !parity;
    case ConditionCode::L:
        return sign != overflow;
    case ConditionCode::GE:
        return sign == overflow;
    case ConditionCode::LE:
        return zero || (sign != overflow);
    case ConditionCode::G:
        return !zero && (sign == overflow);
    case ConditionCode::COUNT:
        break;
    }

    throw std::logic_error{EXECUTOR_INVALID_CONDITION_CODE_MESSAGE};
}

void Executor::require_at_most_one_memory_operand(const Instruction& instruction) const
{
    if (instruction.first_operand().is_memory() && instruction.second_operand().is_memory())
    {
        throw std::logic_error{EXECUTOR_TWO_MEMORY_OPERANDS_MESSAGE};
    }
}

void Executor::require_register_operand(const Operand& operand) const
{
    if (!operand.is_register())
    {
        throw std::logic_error{EXECUTOR_REGISTER_OPERAND_REQUIRED_MESSAGE};
    }
}

void Executor::require_memory_operand(const Operand& operand) const
{
    if (!operand.is_memory())
    {
        throw std::logic_error{EXECUTOR_MEMORY_OPERAND_REQUIRED_MESSAGE};
    }
}

system::TrapController& Executor::require_trap_controller() const
{
    if (this->trap_controller_ == nullptr)
    {
        throw std::logic_error{EXECUTOR_TRAP_CONTROLLER_REQUIRED_MESSAGE};
    }
    return *this->trap_controller_;
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
