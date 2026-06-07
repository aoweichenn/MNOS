#pragma once

#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>

namespace mnos::test::cpu_support
{
namespace cpu = mnos::cpu;

[[nodiscard]] inline cpu::Instruction make_mov_imm(
    const cpu::RegisterId destination,
    const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_mov(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] inline cpu::Instruction make_add_imm(
    const cpu::RegisterId destination,
    const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_add(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] inline cpu::Instruction make_sub_imm(
    const cpu::RegisterId destination,
    const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_sub(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] inline cpu::Instruction make_cmp_imm(const cpu::RegisterId left, const cpu::SQWORD64 right)
{
    return cpu::Instruction::make_cmp(cpu::Operand::reg(left), cpu::Operand::imm(right));
}

[[nodiscard]] inline cpu::Instruction make_jump_imm(const cpu::Opcode opcode, const cpu::SQWORD64 target)
{
    switch (opcode)
    {
    case cpu::Opcode::JMP:
        return cpu::Instruction::make_jmp(cpu::Operand::imm(target));
    case cpu::Opcode::JE:
        return cpu::Instruction::make_je(cpu::Operand::imm(target));
    case cpu::Opcode::JNE:
        return cpu::Instruction::make_jne(cpu::Operand::imm(target));
    case cpu::Opcode::MOV:
    case cpu::Opcode::ADD:
    case cpu::Opcode::SUB:
    case cpu::Opcode::CMP:
    case cpu::Opcode::HLT:
    case cpu::Opcode::COUNT:
        throw std::logic_error{"test helper requires a jump opcode"};
    }

    throw std::logic_error{"test helper received unknown opcode"};
}

[[nodiscard]] inline cpu::Operand make_mem(
    const cpu::RegisterId base_register,
    const cpu::SQWORD64 displacement,
    const cpu::DataSize data_size)
{
    return cpu::Operand::mem(base_register, displacement, data_size);
}
}
