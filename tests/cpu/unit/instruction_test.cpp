#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <support/test_assert.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;

namespace
{
constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_OPCODE = static_cast<cpu::Opcode>(cpu::OPCODE_INSTRUCTION_KIND_COUNT);
constexpr auto TEST_INVALID_OPERAND_KIND = static_cast<cpu::OperandKind>(cpu::OPERAND_KIND_COUNT);

constexpr cpu::SQWORD64 TEST_IMMEDIATE_VALUE = cpu::SQWORD64{-42};
constexpr cpu::SQWORD64 TEST_MEMORY_DISPLACEMENT = cpu::SQWORD64{16};

void test_opcodes()
{
    struct OpcodeCase
    {
        cpu::Opcode opcode;
        std::size_t index;
        std::string_view assembly_name;
    };

    constexpr std::array<OpcodeCase, cpu::OPCODE_INSTRUCTION_KIND_COUNT> OPCODE_CASES{
        OpcodeCase{cpu::Opcode::MOV, 0, "MOV"},   OpcodeCase{cpu::Opcode::ADD, 1, "ADD"},
        OpcodeCase{cpu::Opcode::SUB, 2, "SUB"},   OpcodeCase{cpu::Opcode::CMP, 3, "CMP"},
        OpcodeCase{cpu::Opcode::JMP, 4, "JMP"},   OpcodeCase{cpu::Opcode::JE, 5, "JE"},
        OpcodeCase{cpu::Opcode::JNE, 6, "JNE"},   OpcodeCase{cpu::Opcode::HALT, 7, "HALT"}};

    for (const OpcodeCase test_case : OPCODE_CASES)
    {
        test::check(cpu::is_opcode_valid(test_case.opcode), "opcode should be valid");
        test::check(cpu::opcode_to_index(test_case.opcode) == test_case.index, "opcode index mismatch");
        test::check(cpu::opcode_to_assembly_name(test_case.opcode) == test_case.assembly_name,
                    "opcode assembly name mismatch");
    }

    test::check(!cpu::is_opcode_valid(TEST_INVALID_OPCODE), "invalid opcode should be rejected");
    test::check(cpu::opcode_to_assembly_name(TEST_INVALID_OPCODE) == "<invalid>",
                "invalid opcode name should be stable");
}

void test_operand_kinds()
{
    test::check(cpu::is_operand_kind_valid(cpu::OperandKind::MEMORY), "memory operand kind should be valid");
    test::check(!cpu::is_operand_kind_valid(TEST_INVALID_OPERAND_KIND), "invalid operand kind should be rejected");
    test::check(cpu::operand_kind_to_index(cpu::OperandKind::IMMEDIATE) ==
                    static_cast<std::size_t>(cpu::OperandKind::IMMEDIATE),
                "operand kind index mismatch");
    test::check(cpu::operand_kind_to_assembly_name(cpu::OperandKind::NONE) == "NONE", "NONE operand name mismatch");
    test::check(cpu::operand_kind_to_assembly_name(TEST_INVALID_OPERAND_KIND) == "<invalid>",
                "invalid operand kind should have stable name");
}

void test_operands()
{
    const cpu::Operand none = cpu::Operand::none();
    test::check(none.kind() == cpu::OperandKind::NONE, "none operand kind mismatch");
    test::check(none.is_none(), "none operand predicate mismatch");
    test::check_throws<std::logic_error>(
        [&none]() {
            static_cast<void>(none.register_id());
        },
        "none operand register access");
    test::check_throws<std::logic_error>(
        [&none]() {
            static_cast<void>(none.immediate_value());
        },
        "none operand immediate access");

    const cpu::Operand reg = cpu::Operand::reg(cpu::RegisterId::RCX);
    test::check(reg.kind() == cpu::OperandKind::REGISTER, "register operand kind mismatch");
    test::check(reg.is_register(), "register operand predicate mismatch");
    test::check(reg.register_id() == cpu::RegisterId::RCX, "register operand payload mismatch");
    test::check_throws<std::logic_error>(
        [&reg]() {
            static_cast<void>(reg.memory_data_size());
        },
        "register operand memory data size access");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::Operand::reg(TEST_INVALID_REGISTER_ID));
        },
        "invalid register operand creation");

    const cpu::Operand imm = cpu::Operand::imm(TEST_IMMEDIATE_VALUE);
    test::check(imm.kind() == cpu::OperandKind::IMMEDIATE, "immediate operand kind mismatch");
    test::check(imm.is_immediate(), "immediate operand predicate mismatch");
    test::check(imm.immediate_value() == TEST_IMMEDIATE_VALUE, "immediate payload mismatch");
    test::check_throws<std::logic_error>(
        [&imm]() {
            static_cast<void>(imm.memory_base_register());
        },
        "immediate operand memory access");

    const cpu::Operand mem =
        cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD);
    test::check(mem.kind() == cpu::OperandKind::MEMORY, "memory operand kind mismatch");
    test::check(mem.is_memory(), "memory operand predicate mismatch");
    test::check(mem.memory_base_register() == cpu::RegisterId::RBP, "memory base register mismatch");
    test::check(mem.memory_displacement() == TEST_MEMORY_DISPLACEMENT, "memory displacement mismatch");
    test::check(mem.memory_data_size() == cpu::DataSize::QWORD, "memory data size mismatch");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                cpu::Operand::mem(TEST_INVALID_REGISTER_ID, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD));
        },
        "invalid memory base register");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, TEST_INVALID_DATA_SIZE));
        },
        "invalid memory data size");
}

void test_instruction_factories()
{
    const cpu::Instruction halt = cpu::Instruction::make_halt();
    test::check(halt.opcode() == cpu::Opcode::HALT, "HALT opcode mismatch");
    test::check(halt.first_operand().is_none(), "HALT first operand should be none");
    test::check(halt.second_operand().is_none(), "HALT second operand should be none");

    const cpu::Instruction mov =
        cpu::Instruction::make_mov(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    test::check(mov.opcode() == cpu::Opcode::MOV, "MOV opcode mismatch");
    test::check(mov.first_operand().register_id() == cpu::RegisterId::RAX, "MOV destination mismatch");
    test::check(mov.second_operand().immediate_value() == TEST_IMMEDIATE_VALUE, "MOV source mismatch");

    const cpu::Instruction add =
        cpu::Instruction::make_add(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    test::check(add.opcode() == cpu::Opcode::ADD, "ADD opcode mismatch");
    test::check(add.second_operand().register_id() == cpu::RegisterId::RBX, "ADD source mismatch");

    const cpu::Instruction sub =
        cpu::Instruction::make_sub(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    test::check(sub.opcode() == cpu::Opcode::SUB, "SUB opcode mismatch");

    const cpu::Instruction cmp =
        cpu::Instruction::make_cmp(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    test::check(cmp.opcode() == cpu::Opcode::CMP, "CMP opcode mismatch");

    const cpu::Instruction jump = cpu::Instruction::make_jmp(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    test::check(jump.opcode() == cpu::Opcode::JMP, "JMP opcode mismatch");
    test::check(jump.first_operand().immediate_value() == TEST_MEMORY_DISPLACEMENT, "JMP target mismatch");
    test::check(jump.second_operand().is_none(), "JMP second operand should be none");

    const cpu::Instruction jump_equal = cpu::Instruction::make_je(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    test::check(jump_equal.opcode() == cpu::Opcode::JE, "JE opcode mismatch");

    const cpu::Instruction jump_not_equal = cpu::Instruction::make_jne(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    test::check(jump_not_equal.opcode() == cpu::Opcode::JNE, "JNE opcode mismatch");
}
}

int main()
{
    test_opcodes();
    test_operand_kinds();
    test_operands();
    test_instruction_factories();

    std::cout << "mnos_cpu_instruction_unit_tests passed\n";
    return 0;
}
