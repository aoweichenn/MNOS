#include <array>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_OPCODE = static_cast<cpu::Opcode>(cpu::OPCODE_INSTRUCTION_KIND_COUNT);
constexpr auto TEST_INVALID_OPERAND_KIND = static_cast<cpu::OperandKind>(cpu::OPERAND_KIND_COUNT);

constexpr cpu::SQWORD64 TEST_IMMEDIATE_VALUE = cpu::SQWORD64{-42};
constexpr cpu::SQWORD64 TEST_MEMORY_DISPLACEMENT = cpu::SQWORD64{16};

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
    OpcodeCase{cpu::Opcode::JNE, 6, "JNE"},   OpcodeCase{cpu::Opcode::HLT, 7, "HLT"}};
}

TEST(OpcodeTest, CatalogMapsOpcodesToIndicesAndNames)
{
    for (const OpcodeCase test_case : OPCODE_CASES)
    {
        EXPECT_TRUE(cpu::is_opcode_valid(test_case.opcode));
        EXPECT_THAT(cpu::opcode_to_index(test_case.opcode), Eq(test_case.index));
        EXPECT_THAT(cpu::opcode_to_assembly_name(test_case.opcode), Eq(test_case.assembly_name));
    }

    EXPECT_FALSE(cpu::is_opcode_valid(TEST_INVALID_OPCODE));
    EXPECT_THAT(cpu::opcode_to_assembly_name(TEST_INVALID_OPCODE), Eq(std::string_view{"<invalid>"}));
}

TEST(OperandKindTest, CatalogRejectsInvalidKinds)
{
    EXPECT_TRUE(cpu::is_operand_kind_valid(cpu::OperandKind::MEMORY));
    EXPECT_FALSE(cpu::is_operand_kind_valid(TEST_INVALID_OPERAND_KIND));
    EXPECT_THAT(
        cpu::operand_kind_to_index(cpu::OperandKind::IMMEDIATE),
        Eq(static_cast<std::size_t>(cpu::OperandKind::IMMEDIATE)));
    EXPECT_THAT(cpu::operand_kind_to_assembly_name(cpu::OperandKind::NONE), Eq(std::string_view{"NONE"}));
    EXPECT_THAT(cpu::operand_kind_to_assembly_name(TEST_INVALID_OPERAND_KIND), Eq(std::string_view{"<invalid>"}));
}

TEST(OperandTest, ModelsNoneRegisterImmediateAndMemoryPayloads)
{
    const cpu::Operand none = cpu::Operand::none();
    EXPECT_THAT(none.kind(), Eq(cpu::OperandKind::NONE));
    EXPECT_TRUE(none.is_none());
    EXPECT_THROW(static_cast<void>(none.register_id()), std::logic_error);
    EXPECT_THROW(static_cast<void>(none.immediate_value()), std::logic_error);

    const cpu::Operand reg = cpu::Operand::reg(cpu::RegisterId::RCX);
    EXPECT_THAT(reg.kind(), Eq(cpu::OperandKind::REGISTER));
    EXPECT_TRUE(reg.is_register());
    EXPECT_THAT(reg.register_id(), Eq(cpu::RegisterId::RCX));
    EXPECT_THROW(static_cast<void>(reg.memory_data_size()), std::logic_error);
    EXPECT_THROW(static_cast<void>(cpu::Operand::reg(TEST_INVALID_REGISTER_ID)), std::out_of_range);

    const cpu::Operand imm = cpu::Operand::imm(TEST_IMMEDIATE_VALUE);
    EXPECT_THAT(imm.kind(), Eq(cpu::OperandKind::IMMEDIATE));
    EXPECT_TRUE(imm.is_immediate());
    EXPECT_THAT(imm.immediate_value(), Eq(TEST_IMMEDIATE_VALUE));
    EXPECT_THROW(static_cast<void>(imm.memory_base_register()), std::logic_error);

    const cpu::Operand mem =
        cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD);
    EXPECT_THAT(mem.kind(), Eq(cpu::OperandKind::MEMORY));
    EXPECT_TRUE(mem.is_memory());
    EXPECT_THAT(mem.memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(mem.memory_displacement(), Eq(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(mem.memory_data_size(), Eq(cpu::DataSize::QWORD));
    EXPECT_THROW(
        static_cast<void>(cpu::Operand::mem(TEST_INVALID_REGISTER_ID, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, TEST_INVALID_DATA_SIZE)),
        std::out_of_range);
}

TEST(InstructionTest, FactoryFunctionsCreateExpectedShapes)
{
    const cpu::Instruction halt = cpu::Instruction::make_hlt();
    EXPECT_THAT(halt.opcode(), Eq(cpu::Opcode::HLT));
    EXPECT_TRUE(halt.first_operand().is_none());
    EXPECT_TRUE(halt.second_operand().is_none());

    const cpu::Instruction mov =
        cpu::Instruction::make_mov(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    EXPECT_THAT(mov.opcode(), Eq(cpu::Opcode::MOV));
    EXPECT_THAT(mov.first_operand().register_id(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(mov.second_operand().immediate_value(), Eq(TEST_IMMEDIATE_VALUE));

    const cpu::Instruction add =
        cpu::Instruction::make_add(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    EXPECT_THAT(add.opcode(), Eq(cpu::Opcode::ADD));
    EXPECT_THAT(add.second_operand().register_id(), Eq(cpu::RegisterId::RBX));

    const cpu::Instruction sub =
        cpu::Instruction::make_sub(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    EXPECT_THAT(sub.opcode(), Eq(cpu::Opcode::SUB));

    const cpu::Instruction cmp =
        cpu::Instruction::make_cmp(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    EXPECT_THAT(cmp.opcode(), Eq(cpu::Opcode::CMP));

    const cpu::Instruction jump = cpu::Instruction::make_jmp(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump.opcode(), Eq(cpu::Opcode::JMP));
    EXPECT_THAT(jump.first_operand().immediate_value(), Eq(TEST_MEMORY_DISPLACEMENT));
    EXPECT_TRUE(jump.second_operand().is_none());

    const cpu::Instruction jump_equal = cpu::Instruction::make_je(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump_equal.opcode(), Eq(cpu::Opcode::JE));

    const cpu::Instruction jump_not_equal = cpu::Instruction::make_jne(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump_not_equal.opcode(), Eq(cpu::Opcode::JNE));
}
