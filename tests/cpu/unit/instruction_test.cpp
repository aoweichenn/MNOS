#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/instruction/condition_code.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>

namespace cpu = mnos::cpu;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_COUNT);
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_COUNT);
constexpr auto TEST_INVALID_OPCODE = static_cast<cpu::Opcode>(cpu::OPCODE_COUNT);
constexpr auto TEST_INVALID_OPERAND_KIND = static_cast<cpu::OperandKind>(cpu::OPERAND_KIND_COUNT);
constexpr auto TEST_INVALID_CONDITION_CODE = static_cast<cpu::ConditionCode>(cpu::CONDITION_CODE_COUNT);

constexpr cpu::SignedQword TEST_IMMEDIATE_VALUE = cpu::SignedQword{-42};
constexpr cpu::SignedQword TEST_MEMORY_DISPLACEMENT = cpu::SignedQword{16};
constexpr cpu::Address64 TEST_MEMORY_ABSOLUTE_ADDRESS = cpu::Address64{128};
constexpr cpu_system::InterruptVector TEST_INTERRUPT_VECTOR = cpu_system::InterruptVector::syscall_compat();
constexpr std::uint8_t TEST_MEMORY_INDEX_SCALE = cpu::MEMORY_ADDRESS_SCALE_4;
constexpr std::uint8_t TEST_MEMORY_INVALID_SCALE = 3;

struct OpcodeCase
{
    cpu::Opcode opcode;
    std::size_t index;
    std::string_view assembly_name;
};

constexpr std::array<OpcodeCase, cpu::OPCODE_COUNT> OPCODE_CASES{
    OpcodeCase{cpu::Opcode::MOV, 0, "MOV"},       OpcodeCase{cpu::Opcode::MOVSX, 1, "MOVSX"},
    OpcodeCase{cpu::Opcode::MOVZX, 2, "MOVZX"},   OpcodeCase{cpu::Opcode::LEA, 3, "LEA"},
    OpcodeCase{cpu::Opcode::ADD, 4, "ADD"},       OpcodeCase{cpu::Opcode::SUB, 5, "SUB"},
    OpcodeCase{cpu::Opcode::CMP, 6, "CMP"},       OpcodeCase{cpu::Opcode::INC, 7, "INC"},
    OpcodeCase{cpu::Opcode::DEC, 8, "DEC"},       OpcodeCase{cpu::Opcode::AND, 9, "AND"},
    OpcodeCase{cpu::Opcode::OR, 10, "OR"},        OpcodeCase{cpu::Opcode::XOR, 11, "XOR"},
    OpcodeCase{cpu::Opcode::TEST, 12, "TEST"},    OpcodeCase{cpu::Opcode::PUSH, 13, "PUSH"},
    OpcodeCase{cpu::Opcode::POP, 14, "POP"},      OpcodeCase{cpu::Opcode::CALL, 15, "CALL"},
    OpcodeCase{cpu::Opcode::RET, 16, "RET"},      OpcodeCase{cpu::Opcode::JMP, 17, "JMP"},
    OpcodeCase{cpu::Opcode::JE, 18, "JE"},        OpcodeCase{cpu::Opcode::JNE, 19, "JNE"},
    OpcodeCase{cpu::Opcode::JCC, 20, "JCC"},      OpcodeCase{cpu::Opcode::SETCC, 21, "SETCC"},
    OpcodeCase{cpu::Opcode::CMOVCC, 22, "CMOVCC"}, OpcodeCase{cpu::Opcode::INT, 23, "INT"},
    OpcodeCase{cpu::Opcode::SYSCALL, 24, "SYSCALL"}, OpcodeCase{cpu::Opcode::SYSRET, 25, "SYSRET"},
    OpcodeCase{cpu::Opcode::IRET, 26, "IRET"},    OpcodeCase{cpu::Opcode::HLT, 27, "HLT"}};

struct ConditionCodeCase
{
    cpu::ConditionCode condition;
    std::size_t index;
    std::string_view assembly_suffix;
};

constexpr std::array<ConditionCodeCase, cpu::CONDITION_CODE_COUNT> CONDITION_CODE_CASES{
    ConditionCodeCase{cpu::ConditionCode::O, 0, "O"},
    ConditionCodeCase{cpu::ConditionCode::NO, 1, "NO"},
    ConditionCodeCase{cpu::ConditionCode::B, 2, "B"},
    ConditionCodeCase{cpu::ConditionCode::AE, 3, "AE"},
    ConditionCodeCase{cpu::ConditionCode::E, 4, "E"},
    ConditionCodeCase{cpu::ConditionCode::NE, 5, "NE"},
    ConditionCodeCase{cpu::ConditionCode::BE, 6, "BE"},
    ConditionCodeCase{cpu::ConditionCode::A, 7, "A"},
    ConditionCodeCase{cpu::ConditionCode::S, 8, "S"},
    ConditionCodeCase{cpu::ConditionCode::NS, 9, "NS"},
    ConditionCodeCase{cpu::ConditionCode::P, 10, "P"},
    ConditionCodeCase{cpu::ConditionCode::NP, 11, "NP"},
    ConditionCodeCase{cpu::ConditionCode::L, 12, "L"},
    ConditionCodeCase{cpu::ConditionCode::GE, 13, "GE"},
    ConditionCodeCase{cpu::ConditionCode::LE, 14, "LE"},
    ConditionCodeCase{cpu::ConditionCode::G, 15, "G"}};
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

TEST(ConditionCodeTest, CatalogMapsConditionsToIndicesAndSuffixes)
{
    for (const ConditionCodeCase test_case : CONDITION_CODE_CASES)
    {
        EXPECT_TRUE(cpu::is_condition_code_valid(test_case.condition));
        EXPECT_THAT(cpu::condition_code_to_index(test_case.condition), Eq(test_case.index));
        EXPECT_THAT(cpu::condition_code_to_assembly_suffix(test_case.condition), Eq(test_case.assembly_suffix));
    }

    EXPECT_FALSE(cpu::is_condition_code_valid(TEST_INVALID_CONDITION_CODE));
    EXPECT_THAT(
        cpu::condition_code_to_assembly_suffix(TEST_INVALID_CONDITION_CODE),
        Eq(std::string_view{"<invalid>"}));
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
    EXPECT_THAT(reg.register_data_size(), Eq(cpu::DataSize::QWORD));
    EXPECT_THROW(static_cast<void>(reg.memory_data_size()), std::logic_error);
    EXPECT_THROW(static_cast<void>(cpu::Operand::reg(TEST_INVALID_REGISTER_ID)), std::out_of_range);

    const cpu::Operand byte_reg = cpu::Operand::reg(cpu::RegisterId::RAX, cpu::DataSize::BYTE);
    EXPECT_THAT(byte_reg.register_data_size(), Eq(cpu::DataSize::BYTE));
    EXPECT_THROW(static_cast<void>(cpu::Operand::reg(cpu::RegisterId::RAX, TEST_INVALID_DATA_SIZE)), std::out_of_range);

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

    const cpu::Operand indexed_mem = cpu::Operand::indexed_mem(
        cpu::RegisterId::RBP,
        cpu::RegisterId::RAX,
        TEST_MEMORY_INDEX_SCALE,
        TEST_MEMORY_DISPLACEMENT,
        cpu::DataSize::QWORD);
    EXPECT_TRUE(indexed_mem.memory_has_base_register());
    EXPECT_TRUE(indexed_mem.memory_has_index_register());
    EXPECT_FALSE(indexed_mem.memory_has_absolute_address());
    EXPECT_THAT(indexed_mem.memory_base_register(), Eq(cpu::RegisterId::RBP));
    EXPECT_THAT(indexed_mem.memory_index_register(), Eq(cpu::RegisterId::RAX));
    EXPECT_THAT(indexed_mem.memory_scale(), Eq(TEST_MEMORY_INDEX_SCALE));

    const cpu::Operand index_only_mem = cpu::Operand::mem(
        cpu::MemoryAddress::index_displacement(
            cpu::RegisterId::RAX,
            TEST_MEMORY_INDEX_SCALE,
            TEST_MEMORY_DISPLACEMENT),
        cpu::DataSize::QWORD);
    EXPECT_FALSE(index_only_mem.memory_has_base_register());
    EXPECT_TRUE(index_only_mem.memory_has_index_register());
    EXPECT_THROW(static_cast<void>(index_only_mem.memory_base_register()), std::logic_error);

    const cpu::Operand absolute_mem = cpu::Operand::absolute_mem(TEST_MEMORY_ABSOLUTE_ADDRESS, cpu::DataSize::QWORD);
    EXPECT_FALSE(absolute_mem.memory_has_base_register());
    EXPECT_FALSE(absolute_mem.memory_has_index_register());
    EXPECT_TRUE(absolute_mem.memory_has_absolute_address());
    EXPECT_THAT(absolute_mem.memory_absolute_address(), Eq(TEST_MEMORY_ABSOLUTE_ADDRESS));
    EXPECT_THROW(static_cast<void>(absolute_mem.memory_index_register()), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(cpu::Operand::indexed_mem(
            cpu::RegisterId::RBP,
            cpu::RegisterId::RAX,
            TEST_MEMORY_INVALID_SCALE,
            TEST_MEMORY_DISPLACEMENT,
            cpu::DataSize::QWORD)),
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
    EXPECT_FALSE(mov.has_condition_code());

    const cpu::Instruction movsx = cpu::Instruction::make_movsx(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::reg(cpu::RegisterId::RBX, cpu::DataSize::BYTE));
    EXPECT_THAT(movsx.opcode(), Eq(cpu::Opcode::MOVSX));
    EXPECT_THAT(movsx.second_operand().register_data_size(), Eq(cpu::DataSize::BYTE));

    const cpu::Instruction lea = cpu::Instruction::make_lea(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD));
    EXPECT_THAT(lea.opcode(), Eq(cpu::Opcode::LEA));

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

    const cpu::Instruction bit_test =
        cpu::Instruction::make_test(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    EXPECT_THAT(bit_test.opcode(), Eq(cpu::Opcode::TEST));

    const cpu::Instruction push = cpu::Instruction::make_push(cpu::Operand::reg(cpu::RegisterId::RAX));
    EXPECT_THAT(push.opcode(), Eq(cpu::Opcode::PUSH));

    const cpu::Instruction ret = cpu::Instruction::make_ret();
    EXPECT_THAT(ret.opcode(), Eq(cpu::Opcode::RET));
    EXPECT_TRUE(ret.first_operand().is_none());

    const cpu::Instruction jump = cpu::Instruction::make_jmp(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump.opcode(), Eq(cpu::Opcode::JMP));
    EXPECT_THAT(jump.first_operand().immediate_value(), Eq(TEST_MEMORY_DISPLACEMENT));
    EXPECT_TRUE(jump.second_operand().is_none());

    const cpu::Instruction jump_equal = cpu::Instruction::make_je(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump_equal.opcode(), Eq(cpu::Opcode::JE));
    EXPECT_TRUE(jump_equal.has_condition_code());
    EXPECT_THAT(jump_equal.condition_code(), Eq(cpu::ConditionCode::E));

    const cpu::Instruction jump_not_equal = cpu::Instruction::make_jne(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump_not_equal.opcode(), Eq(cpu::Opcode::JNE));

    const cpu::Instruction jump_less =
        cpu::Instruction::make_jcc(cpu::ConditionCode::L, cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    EXPECT_THAT(jump_less.opcode(), Eq(cpu::Opcode::JCC));
    EXPECT_THAT(jump_less.condition_code(), Eq(cpu::ConditionCode::L));

    const cpu::Instruction set_greater =
        cpu::Instruction::make_setcc(cpu::ConditionCode::G, cpu::Operand::reg(cpu::RegisterId::RAX, cpu::DataSize::BYTE));
    EXPECT_THAT(set_greater.opcode(), Eq(cpu::Opcode::SETCC));
    EXPECT_THAT(set_greater.condition_code(), Eq(cpu::ConditionCode::G));

    const cpu::Instruction cmov_above = cpu::Instruction::make_cmovcc(
        cpu::ConditionCode::A,
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::reg(cpu::RegisterId::RBX));
    EXPECT_THAT(cmov_above.opcode(), Eq(cpu::Opcode::CMOVCC));
    EXPECT_THAT(cmov_above.condition_code(), Eq(cpu::ConditionCode::A));
    EXPECT_THROW(
        static_cast<void>(cpu::Instruction::make_jcc(TEST_INVALID_CONDITION_CODE, cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT))),
        std::out_of_range);

    const cpu::Instruction software_interrupt = cpu::Instruction::make_int(TEST_INTERRUPT_VECTOR);
    EXPECT_THAT(software_interrupt.opcode(), Eq(cpu::Opcode::INT));
    EXPECT_THAT(
        software_interrupt.first_operand().immediate_value(),
        Eq(static_cast<cpu::SignedQword>(TEST_INTERRUPT_VECTOR.value())));

    EXPECT_THAT(cpu::Instruction::make_syscall().opcode(), Eq(cpu::Opcode::SYSCALL));
    EXPECT_THAT(cpu::Instruction::make_sysret().opcode(), Eq(cpu::Opcode::SYSRET));
    EXPECT_THAT(cpu::Instruction::make_iret().opcode(), Eq(cpu::Opcode::IRET));
}
