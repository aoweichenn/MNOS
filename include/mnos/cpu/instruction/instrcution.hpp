//
// Created by aoweichen on 2026/6/6.
//

#pragma once
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>

namespace mnos
{
class Instruction{
public:
    [[nodiscard]] static Instruction make_halt() noexcept;
    [[nodiscard]] static Instruction make_mov(const Operand& first_operand, const Operand& second_operand) noexcept;
    [[nodiscard]] static Instruction make_add(const Operand& first_operand, const Operand& second_operand) noexcept;
    [[nodiscard]] static Instruction make_sub(const Operand& first_operand, const Operand& second_operand) noexcept;
    [[nodiscard]] static Instruction make_cmp(const Operand& first_operand, const Operand& second_operand) noexcept;
    [[nodiscard]] static Instruction make_jmp(const Operand& target_operand) noexcept;
    [[nodiscard]] static Instruction make_je(const Operand& target_operand) noexcept;
    [[nodiscard]] static Instruction make_jne(const Operand& target_operand) noexcept;

    [[nodiscard]] Opcode opcode() const noexcept;
    [[nodiscard]] const Operand& first_operand() const noexcept;
    [[nodiscard]] const Operand& second_operand() const noexcept;

private:
    Instruction(Opcode opcode, const Operand& first_operand, const Operand& second_operand);

    Opcode _opcode = Opcode::HALT;
    Operand _first_operand = Operand::none();
    Operand _second_operand = Operand::none();
};
}
