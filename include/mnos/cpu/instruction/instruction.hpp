#pragma once

#include <mnos/cpu/instruction/condition_code.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>

namespace mnos::cpu
{
class Instruction
{
public:
    [[nodiscard]] static Instruction make_hlt() noexcept;
    [[nodiscard]] static Instruction make_mov(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_movsx(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_movzx(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_lea(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_add(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_sub(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_cmp(Operand left, Operand right) noexcept;
    [[nodiscard]] static Instruction make_inc(Operand destination) noexcept;
    [[nodiscard]] static Instruction make_dec(Operand destination) noexcept;
    [[nodiscard]] static Instruction make_and(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_or(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_xor(Operand destination, Operand source) noexcept;
    [[nodiscard]] static Instruction make_test(Operand left, Operand right) noexcept;
    [[nodiscard]] static Instruction make_cmpxchg(Operand destination, Operand source, bool locked = false) noexcept;
    [[nodiscard]] static Instruction make_xadd(Operand destination, Operand source, bool locked = false) noexcept;
    [[nodiscard]] static Instruction make_mfence() noexcept;
    [[nodiscard]] static Instruction make_invlpg(Operand address) noexcept;
    [[nodiscard]] static Instruction make_push(Operand source) noexcept;
    [[nodiscard]] static Instruction make_pop(Operand destination) noexcept;
    [[nodiscard]] static Instruction make_call(Operand target) noexcept;
    [[nodiscard]] static Instruction make_ret() noexcept;
    [[nodiscard]] static Instruction make_jmp(Operand target) noexcept;
    [[nodiscard]] static Instruction make_je(Operand target) noexcept;
    [[nodiscard]] static Instruction make_jne(Operand target) noexcept;
    [[nodiscard]] static Instruction make_jcc(ConditionCode condition, Operand target);
    [[nodiscard]] static Instruction make_setcc(ConditionCode condition, Operand destination);
    [[nodiscard]] static Instruction make_cmovcc(ConditionCode condition, Operand destination, Operand source);
    [[nodiscard]] static Instruction make_int(system::InterruptVector vector) noexcept;
    [[nodiscard]] static Instruction make_syscall() noexcept;
    [[nodiscard]] static Instruction make_sysret() noexcept;
    [[nodiscard]] static Instruction make_iret() noexcept;

    [[nodiscard]] Opcode opcode() const noexcept;
    [[nodiscard]] bool is_locked() const noexcept;
    [[nodiscard]] bool has_condition_code() const noexcept;
    [[nodiscard]] ConditionCode condition_code() const noexcept;
    [[nodiscard]] const Operand& first_operand() const noexcept;
    [[nodiscard]] const Operand& second_operand() const noexcept;

private:
    Instruction(Opcode opcode, Operand first_operand, Operand second_operand, bool locked = false) noexcept;
    Instruction(
        Opcode opcode,
        ConditionCode condition,
        Operand first_operand,
        Operand second_operand,
        bool locked = false) noexcept;

    Opcode opcode_ = Opcode::HLT;
    ConditionCode condition_ = ConditionCode::COUNT;
    Operand first_operand_ = Operand::none();
    Operand second_operand_ = Operand::none();
    bool locked_ = false;
};
}
