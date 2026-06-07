#pragma once

#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>

namespace mnos
{
class Instruction
{
public:
    [[nodiscard]] static Instruction make_halt() noexcept;
    [[nodiscard]] static Instruction make_mov(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_add(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_sub(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_cmp(Operand left, Operand right) noexcept;
    [[nodiscard]] static Instruction make_jmp(Operand target) noexcept;
    [[nodiscard]] static Instruction make_je(Operand target) noexcept;
    [[nodiscard]] static Instruction make_jne(Operand target) noexcept;

    [[nodiscard]] Opcode opcode() const noexcept;
    [[nodiscard]] const Operand& first_operand() const noexcept;
    [[nodiscard]] const Operand& second_operand() const noexcept;

private:
    Instruction(Opcode opcode, Operand first_operand, Operand second_operand) noexcept;

    Opcode opcode_ = Opcode::HALT;
    Operand first_operand_ = Operand::none();
    Operand second_operand_ = Operand::none();
};
}
