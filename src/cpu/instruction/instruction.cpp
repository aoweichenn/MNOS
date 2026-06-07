#include <stdexcept>
#include <utility>

#include <mnos/cpu/instruction/instruction.hpp>

namespace
{
constexpr const char* INSTRUCTION_INVALID_CONDITION_CODE_MESSAGE = "instruction condition code is invalid";

void require_condition_code(const mnos::cpu::ConditionCode condition)
{
    if (!mnos::cpu::is_condition_code_valid(condition))
    {
        throw std::out_of_range{INSTRUCTION_INVALID_CONDITION_CODE_MESSAGE};
    }
}
}

namespace mnos::cpu
{
Instruction::Instruction(
    const Opcode opcode,
    Operand first_operand,
    Operand second_operand,
    const bool locked) noexcept :
    opcode_(opcode),
    first_operand_(std::move(first_operand)), second_operand_(std::move(second_operand)), locked_(locked)
{
}

Instruction::Instruction(
    const Opcode opcode,
    const ConditionCode condition,
    Operand first_operand,
    Operand second_operand,
    const bool locked) noexcept :
    opcode_(opcode),
    condition_(condition),
    first_operand_(std::move(first_operand)), second_operand_(std::move(second_operand)), locked_(locked)
{
}

Instruction Instruction::make_hlt() noexcept
{
    return Instruction{Opcode::HLT, Operand::none(), Operand::none()};
}

Instruction Instruction::make_mov(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::MOV, std::move(destination), std::move(source)};
}

Instruction Instruction::make_movsx(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::MOVSX, std::move(destination), std::move(source)};
}

Instruction Instruction::make_movzx(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::MOVZX, std::move(destination), std::move(source)};
}

Instruction Instruction::make_lea(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::LEA, std::move(destination), std::move(source)};
}

Instruction Instruction::make_add(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::ADD, std::move(destination), std::move(source)};
}

Instruction Instruction::make_sub(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::SUB, std::move(destination), std::move(source)};
}

Instruction Instruction::make_cmp(Operand left, Operand right) noexcept
{
    return Instruction{Opcode::CMP, std::move(left), std::move(right)};
}

Instruction Instruction::make_inc(Operand destination) noexcept
{
    return Instruction{Opcode::INC, std::move(destination), Operand::none()};
}

Instruction Instruction::make_dec(Operand destination) noexcept
{
    return Instruction{Opcode::DEC, std::move(destination), Operand::none()};
}

Instruction Instruction::make_and(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::AND, std::move(destination), std::move(source)};
}

Instruction Instruction::make_or(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::OR, std::move(destination), std::move(source)};
}

Instruction Instruction::make_xor(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::XOR, std::move(destination), std::move(source)};
}

Instruction Instruction::make_test(Operand left, Operand right) noexcept
{
    return Instruction{Opcode::TEST, std::move(left), std::move(right)};
}

Instruction Instruction::make_cmpxchg(Operand destination, Operand source, const bool locked) noexcept
{
    return Instruction{Opcode::CMPXCHG, std::move(destination), std::move(source), locked};
}

Instruction Instruction::make_xadd(Operand destination, Operand source, const bool locked) noexcept
{
    return Instruction{Opcode::XADD, std::move(destination), std::move(source), locked};
}

Instruction Instruction::make_mfence() noexcept
{
    return Instruction{Opcode::MFENCE, Operand::none(), Operand::none()};
}

Instruction Instruction::make_push(Operand source) noexcept
{
    return Instruction{Opcode::PUSH, std::move(source), Operand::none()};
}

Instruction Instruction::make_pop(Operand destination) noexcept
{
    return Instruction{Opcode::POP, std::move(destination), Operand::none()};
}

Instruction Instruction::make_call(Operand target) noexcept
{
    return Instruction{Opcode::CALL, std::move(target), Operand::none()};
}

Instruction Instruction::make_ret() noexcept
{
    return Instruction{Opcode::RET, Operand::none(), Operand::none()};
}

Instruction Instruction::make_jmp(Operand target) noexcept
{
    return Instruction{Opcode::JMP, std::move(target), Operand::none()};
}

Instruction Instruction::make_je(Operand target) noexcept
{
    return Instruction{Opcode::JE, ConditionCode::E, std::move(target), Operand::none()};
}

Instruction Instruction::make_jne(Operand target) noexcept
{
    return Instruction{Opcode::JNE, ConditionCode::NE, std::move(target), Operand::none()};
}

Instruction Instruction::make_jcc(const ConditionCode condition, Operand target)
{
    require_condition_code(condition);
    return Instruction{Opcode::JCC, condition, std::move(target), Operand::none()};
}

Instruction Instruction::make_setcc(const ConditionCode condition, Operand destination)
{
    require_condition_code(condition);
    return Instruction{Opcode::SETCC, condition, std::move(destination), Operand::none()};
}

Instruction Instruction::make_cmovcc(const ConditionCode condition, Operand destination, Operand source)
{
    require_condition_code(condition);
    return Instruction{Opcode::CMOVCC, condition, std::move(destination), std::move(source)};
}

Instruction Instruction::make_int(const system::InterruptVector vector) noexcept
{
    return Instruction{
        Opcode::INT,
        Operand::imm(static_cast<SignedQword>(vector.value())),
        Operand::none()};
}

Instruction Instruction::make_syscall() noexcept
{
    return Instruction{Opcode::SYSCALL, Operand::none(), Operand::none()};
}

Instruction Instruction::make_sysret() noexcept
{
    return Instruction{Opcode::SYSRET, Operand::none(), Operand::none()};
}

Instruction Instruction::make_iret() noexcept
{
    return Instruction{Opcode::IRET, Operand::none(), Operand::none()};
}

Opcode Instruction::opcode() const noexcept
{
    return this->opcode_;
}

bool Instruction::is_locked() const noexcept
{
    return this->locked_;
}

bool Instruction::has_condition_code() const noexcept
{
    return this->condition_ != ConditionCode::COUNT;
}

ConditionCode Instruction::condition_code() const noexcept
{
    return this->condition_;
}

const Operand& Instruction::first_operand() const noexcept
{
    return this->first_operand_;
}

const Operand& Instruction::second_operand() const noexcept
{
    return this->second_operand_;
}
}
