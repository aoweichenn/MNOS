//
// Created by aoweichen on 2026/6/6.
//
#include <stdexcept>
#include <mnos/cpu/instruction/instrcution.hpp>

namespace
{
constexpr const char* INSTRUCTION_CREATE_INVALID_OPCODE_MESSAGE = "instruction invalid opcode";

class Helper{
public:
    static void validate_instruction_opcode(const mnos::Opcode opcode)
    {
        if (!mnos::is_opcode_valid(opcode))
        {
            throw std::out_of_range{INSTRUCTION_CREATE_INVALID_OPCODE_MESSAGE};
        }
    }
};
}

namespace mnos
{
Instruction::Instruction(const Opcode opcode, const Operand& first_operand, const Operand& second_operand) :
    _opcode(opcode), _first_operand(first_operand), _second_operand(second_operand)
{
    Helper::validate_instruction_opcode(opcode);
}

Instruction Instruction::make_halt() noexcept
{
    return Instruction{Opcode::HALT, Operand::none(), Operand::none()};
}

Instruction Instruction::make_mov(const Operand& first_operand, const Operand& second_operand) noexcept
{
    return Instruction{Opcode::MOV, first_operand, second_operand};
}

Instruction Instruction::make_add(const Operand& first_operand, const Operand& second_operand) noexcept
{
    return Instruction{Opcode::ADD, first_operand, second_operand};
}

Instruction Instruction::make_sub(const Operand& first_operand, const Operand& second_operand) noexcept
{
    return Instruction{Opcode::SUB, first_operand, second_operand};
}

Instruction Instruction::make_cmp(const Operand& first_operand, const Operand& second_operand) noexcept
{
    return Instruction{Opcode::CMP, first_operand, second_operand};
}

Instruction Instruction::make_jmp(const Operand& target_operand) noexcept
{
    return Instruction{Opcode::JMP, target_operand, Operand::none()};
}

Instruction Instruction::make_je(const Operand& target_operand) noexcept
{
    return Instruction{Opcode::JE, target_operand, Operand::none()};
}

Instruction Instruction::make_jne(const Operand& target_operand) noexcept
{
    return Instruction{Opcode::JNE, target_operand, Operand::none()};
}

Opcode Instruction::opcode() const noexcept
{
    return this->_opcode;
}

const Operand& Instruction::first_operand() const noexcept
{
    return this->_first_operand;
}

const Operand& Instruction::second_operand() const noexcept
{
    return this->_second_operand;
}
}
