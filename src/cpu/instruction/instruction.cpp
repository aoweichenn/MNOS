#include <utility>

#include <mnos/cpu/instruction/instruction.hpp>

namespace mnos::cpu
{
Instruction::Instruction(const Opcode opcode, Operand first_operand, Operand second_operand) noexcept :
    opcode_(opcode), first_operand_(std::move(first_operand)), second_operand_(std::move(second_operand))
{
}

Instruction Instruction::make_halt() noexcept
{
    return Instruction{Opcode::HALT, Operand::none(), Operand::none()};
}

Instruction Instruction::make_mov(Operand destination, Operand source) noexcept
{
    return Instruction{Opcode::MOV, std::move(destination), std::move(source)};
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

Instruction Instruction::make_jmp(Operand target) noexcept
{
    return Instruction{Opcode::JMP, std::move(target), Operand::none()};
}

Instruction Instruction::make_je(Operand target) noexcept
{
    return Instruction{Opcode::JE, std::move(target), Operand::none()};
}

Instruction Instruction::make_jne(Operand target) noexcept
{
    return Instruction{Opcode::JNE, std::move(target), Operand::none()};
}

Opcode Instruction::opcode() const noexcept
{
    return this->opcode_;
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
