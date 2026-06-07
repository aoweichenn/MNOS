#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/bank.hpp>
#include <mnos/cpu/register/id.hpp>

namespace
{
constexpr auto TEST_INVALID_DATA_SIZE = static_cast<mnos::DataSize>(mnos::DATA_SIZE_KIND_COUNT);
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<mnos::RegisterId>(mnos::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_FLAG_ID = static_cast<mnos::FlagId>(mnos::FLAG_ID_STATUS_FLAG_COUNT);
constexpr auto TEST_INVALID_OPCODE = static_cast<mnos::Opcode>(mnos::OPCODE_INSTRUCTION_KIND_COUNT);
constexpr auto TEST_INVALID_OPERAND_KIND = static_cast<mnos::OperandKind>(mnos::OPERAND_KIND_COUNT);

constexpr mnos::UQWORD64 TEST_REGISTER_VALUE = mnos::UQWORD64{0x1234ABCDULL};
constexpr mnos::UQWORD64 TEST_SECOND_REGISTER_VALUE = mnos::UQWORD64{0xFEDCBA98ULL};
constexpr mnos::SQWORD64 TEST_IMMEDIATE_VALUE = mnos::SQWORD64{-42};
constexpr mnos::SQWORD64 TEST_MEMORY_DISPLACEMENT = mnos::SQWORD64{16};
constexpr mnos::UQWORD64 TEST_ONE_BIT = mnos::UQWORD64{1};
constexpr mnos::UQWORD64 TEST_QWORD_SIGN_MASK =
    TEST_ONE_BIT << (mnos::DATA_SIZE_QWORD_BITS - std::size_t{1});
constexpr mnos::UQWORD64 TEST_CF_MASK = TEST_ONE_BIT << mnos::FLAG_ID_CF_BIT_INDEX;

void check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error{std::string{message}};
    }
}

template <typename Exception, typename Callable>
void check_throws(Callable&& callable, const std::string_view message)
{
    try
    {
        std::forward<Callable>(callable)();
    }
    catch (const Exception&)
    {
        return;
    }
    catch (...)
    {
        throw std::runtime_error{"unexpected exception type: " + std::string{message}};
    }

    throw std::runtime_error{"expected exception was not thrown: " + std::string{message}};
}

void test_data_size()
{
    struct DataSizeCase
    {
        mnos::DataSize size;
        std::size_t bits;
        std::size_t bytes;
        std::string_view assembly_name;
    };

    constexpr std::array<DataSizeCase, mnos::DATA_SIZE_KIND_COUNT> DATA_SIZE_CASES{
        DataSizeCase{mnos::DataSize::BYTE, mnos::DATA_SIZE_BYTE_BITS, mnos::DATA_SIZE_BYTE_BYTES, "BYTE"},
        DataSizeCase{mnos::DataSize::WORD, mnos::DATA_SIZE_WORD_BITS, mnos::DATA_SIZE_WORD_BYTES, "WORD"},
        DataSizeCase{mnos::DataSize::DWORD, mnos::DATA_SIZE_DWORD_BITS, mnos::DATA_SIZE_DWORD_BYTES, "DWORD"},
        DataSizeCase{mnos::DataSize::QWORD, mnos::DATA_SIZE_QWORD_BITS, mnos::DATA_SIZE_QWORD_BYTES, "QWORD"}};

    for (const DataSizeCase test_case : DATA_SIZE_CASES)
    {
        check(mnos::is_data_size_valid(test_case.size), "data size should be valid");
        check(mnos::data_size_to_bits(test_case.size) == test_case.bits, "data size bits mismatch");
        check(mnos::data_size_to_bytes(test_case.size) == test_case.bytes, "data size bytes mismatch");
        check(mnos::data_size_to_assembly_name(test_case.size) == test_case.assembly_name,
              "data size assembly name mismatch");
    }

    check(!mnos::is_data_size_valid(TEST_INVALID_DATA_SIZE), "invalid data size should be rejected");
    check(mnos::data_size_to_assembly_name(TEST_INVALID_DATA_SIZE) == "<invalid>",
          "invalid data size name should be stable");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(mnos::data_size_to_bits(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bits access");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(mnos::data_size_to_bytes(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bytes access");
}

void test_register_ids_and_bank()
{
    struct RegisterCase
    {
        mnos::RegisterId id;
        std::size_t index;
        std::string_view assembly_name;
    };

    constexpr std::array<RegisterCase, mnos::REGISTER_ID_GENERAL_REGISTER_COUNT> REGISTER_CASES{
        RegisterCase{mnos::RegisterId::RAX, 0, "RAX"},   RegisterCase{mnos::RegisterId::RBX, 1, "RBX"},
        RegisterCase{mnos::RegisterId::RCX, 2, "RCX"},   RegisterCase{mnos::RegisterId::RDX, 3, "RDX"},
        RegisterCase{mnos::RegisterId::RSI, 4, "RSI"},   RegisterCase{mnos::RegisterId::RDI, 5, "RDI"},
        RegisterCase{mnos::RegisterId::RBP, 6, "RBP"},   RegisterCase{mnos::RegisterId::RSP, 7, "RSP"},
        RegisterCase{mnos::RegisterId::R8, 8, "R8"},     RegisterCase{mnos::RegisterId::R9, 9, "R9"},
        RegisterCase{mnos::RegisterId::R10, 10, "R10"},  RegisterCase{mnos::RegisterId::R11, 11, "R11"},
        RegisterCase{mnos::RegisterId::R12, 12, "R12"},  RegisterCase{mnos::RegisterId::R13, 13, "R13"},
        RegisterCase{mnos::RegisterId::R14, 14, "R14"},  RegisterCase{mnos::RegisterId::R15, 15, "R15"}};

    for (const RegisterCase test_case : REGISTER_CASES)
    {
        check(mnos::is_register_id_valid(test_case.id), "register id should be valid");
        check(mnos::register_id_to_index(test_case.id) == test_case.index, "register index mismatch");
        check(mnos::register_id_to_assembly_name(test_case.id) == test_case.assembly_name,
              "register assembly name mismatch");
    }

    check(!mnos::is_register_id_valid(TEST_INVALID_REGISTER_ID), "invalid register id should be rejected");
    check(mnos::register_id_to_assembly_name(TEST_INVALID_REGISTER_ID) == "<invalid>",
          "invalid register name should be stable");

    mnos::RegisterBank bank;
    check(bank.read(mnos::RegisterId::RAX) == mnos::UQWORD64{0}, "register bank should zero initialize");
    bank.write(mnos::RegisterId::RAX, TEST_REGISTER_VALUE);
    bank.write(mnos::RegisterId::R15, TEST_SECOND_REGISTER_VALUE);
    check(bank.read(mnos::RegisterId::RAX) == TEST_REGISTER_VALUE, "register bank RAX read mismatch");
    check(bank.read(mnos::RegisterId::R15) == TEST_SECOND_REGISTER_VALUE, "register bank R15 read mismatch");
    check_throws<std::out_of_range>(
        [&bank]() {
            static_cast<void>(bank.read(TEST_INVALID_REGISTER_ID));
        },
        "invalid register read");
    check_throws<std::out_of_range>(
        [&bank]() {
            bank.write(TEST_INVALID_REGISTER_ID, TEST_REGISTER_VALUE);
        },
        "invalid register write");
}

void test_flags()
{
    struct FlagCase
    {
        mnos::FlagId id;
        std::size_t index;
        std::size_t bit_index;
        std::string_view assembly_name;
    };

    constexpr std::array<FlagCase, mnos::FLAG_ID_STATUS_FLAG_COUNT> FLAG_CASES{
        FlagCase{mnos::FlagId::CF, 0, mnos::FLAG_ID_CF_BIT_INDEX, "CF"},
        FlagCase{mnos::FlagId::ZF, 1, mnos::FLAG_ID_ZF_BIT_INDEX, "ZF"},
        FlagCase{mnos::FlagId::SF, 2, mnos::FLAG_ID_SF_BIT_INDEX, "SF"},
        FlagCase{mnos::FlagId::OF, 3, mnos::FLAG_ID_OF_BIT_INDEX, "OF"}};

    for (const FlagCase test_case : FLAG_CASES)
    {
        check(mnos::is_flag_id_valid(test_case.id), "flag id should be valid");
        check(mnos::flag_id_to_index(test_case.id) == test_case.index, "flag index mismatch");
        check(mnos::flag_id_to_bit_index(test_case.id) == test_case.bit_index, "flag bit index mismatch");
        check(mnos::flag_id_to_assembly_name(test_case.id) == test_case.assembly_name,
              "flag assembly name mismatch");
    }

    check(!mnos::is_flag_id_valid(TEST_INVALID_FLAG_ID), "invalid flag id should be rejected");
    check(mnos::flag_id_to_assembly_name(TEST_INVALID_FLAG_ID) == "<invalid>",
          "invalid flag name should be stable");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(mnos::flag_id_to_bit_index(TEST_INVALID_FLAG_ID));
        },
        "invalid flag bit index");

    mnos::Rflags flags;
    check(flags.raw_bits() == mnos::UQWORD64{0}, "rflags should zero initialize");
    flags.write(mnos::FlagId::CF, true);
    check(flags.read(mnos::FlagId::CF), "CF should be set");
    check(flags.raw_bits() == TEST_CF_MASK, "CF raw bit mismatch");
    flags.write(mnos::FlagId::CF, false);
    check(!flags.read(mnos::FlagId::CF), "CF should be cleared");

    flags.update_zero_sign_from_qword(mnos::UQWORD64{0});
    check(flags.read(mnos::FlagId::ZF), "zero result should set ZF");
    check(!flags.read(mnos::FlagId::SF), "zero result should clear SF");

    flags.update_zero_sign_from_qword(TEST_QWORD_SIGN_MASK);
    check(!flags.read(mnos::FlagId::ZF), "non-zero result should clear ZF");
    check(flags.read(mnos::FlagId::SF), "sign bit should set SF");

    flags.clear_status_flags();
    check(flags.raw_bits() == mnos::UQWORD64{0}, "clear status flags should clear raw bits");
    check_throws<std::out_of_range>(
        [&flags]() {
            static_cast<void>(flags.read(TEST_INVALID_FLAG_ID));
        },
        "invalid flag read");
    check_throws<std::out_of_range>(
        [&flags]() {
            flags.write(TEST_INVALID_FLAG_ID, true);
        },
        "invalid flag write");
}

void test_opcodes()
{
    struct OpcodeCase
    {
        mnos::Opcode opcode;
        std::size_t index;
        std::string_view assembly_name;
    };

    constexpr std::array<OpcodeCase, mnos::OPCODE_INSTRUCTION_KIND_COUNT> OPCODE_CASES{
        OpcodeCase{mnos::Opcode::MOV, 0, "MOV"},   OpcodeCase{mnos::Opcode::ADD, 1, "ADD"},
        OpcodeCase{mnos::Opcode::SUB, 2, "SUB"},   OpcodeCase{mnos::Opcode::CMP, 3, "CMP"},
        OpcodeCase{mnos::Opcode::JMP, 4, "JMP"},   OpcodeCase{mnos::Opcode::JE, 5, "JE"},
        OpcodeCase{mnos::Opcode::JNE, 6, "JNE"},   OpcodeCase{mnos::Opcode::HALT, 7, "HALT"}};

    for (const OpcodeCase test_case : OPCODE_CASES)
    {
        check(mnos::is_opcode_valid(test_case.opcode), "opcode should be valid");
        check(mnos::opcode_to_index(test_case.opcode) == test_case.index, "opcode index mismatch");
        check(mnos::opcode_to_assembly_name(test_case.opcode) == test_case.assembly_name,
              "opcode assembly name mismatch");
    }

    check(!mnos::is_opcode_valid(TEST_INVALID_OPCODE), "invalid opcode should be rejected");
    check(mnos::opcode_to_assembly_name(TEST_INVALID_OPCODE) == "<invalid>",
          "invalid opcode name should be stable");
}

void test_operands()
{
    check(mnos::is_operand_kind_valid(mnos::OperandKind::MEMORY), "memory operand kind should be valid");
    check(!mnos::is_operand_kind_valid(TEST_INVALID_OPERAND_KIND), "invalid operand kind should be rejected");
    check(mnos::operand_kind_to_index(mnos::OperandKind::IMMEDIATE) ==
              static_cast<std::size_t>(mnos::OperandKind::IMMEDIATE),
          "operand kind index mismatch");
    check(mnos::operand_kind_to_assembly_name(mnos::OperandKind::NONE) == "NONE", "NONE operand name mismatch");
    check(mnos::operand_kind_to_assembly_name(TEST_INVALID_OPERAND_KIND) == "<invalid>",
          "invalid operand kind name should be stable");

    const mnos::Operand none = mnos::Operand::none();
    check(none.kind() == mnos::OperandKind::NONE, "none operand kind mismatch");
    check(none.is_none(), "none operand predicate mismatch");
    check_throws<std::logic_error>(
        [&none]() {
            static_cast<void>(none.register_id());
        },
        "none operand register access");
    check_throws<std::logic_error>(
        [&none]() {
            static_cast<void>(none.immediate_value());
        },
        "none operand immediate access");

    const mnos::Operand reg = mnos::Operand::reg(mnos::RegisterId::RCX);
    check(reg.kind() == mnos::OperandKind::REGISTER, "register operand kind mismatch");
    check(reg.is_register(), "register operand predicate mismatch");
    check(reg.register_id() == mnos::RegisterId::RCX, "register operand payload mismatch");
    check_throws<std::logic_error>(
        [&reg]() {
            static_cast<void>(reg.memory_data_size());
        },
        "register operand memory data size access");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(mnos::Operand::reg(TEST_INVALID_REGISTER_ID));
        },
        "invalid register operand creation");

    const mnos::Operand imm = mnos::Operand::imm(TEST_IMMEDIATE_VALUE);
    check(imm.kind() == mnos::OperandKind::IMMEDIATE, "immediate operand kind mismatch");
    check(imm.is_immediate(), "immediate operand predicate mismatch");
    check(imm.immediate_value() == TEST_IMMEDIATE_VALUE, "immediate payload mismatch");
    check_throws<std::logic_error>(
        [&imm]() {
            static_cast<void>(imm.memory_base_register());
        },
        "immediate operand memory access");

    const mnos::Operand mem =
        mnos::Operand::mem(mnos::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, mnos::DataSize::QWORD);
    check(mem.kind() == mnos::OperandKind::MEMORY, "memory operand kind mismatch");
    check(mem.is_memory(), "memory operand predicate mismatch");
    check(mem.memory_base_register() == mnos::RegisterId::RBP, "memory base register mismatch");
    check(mem.memory_displacement() == TEST_MEMORY_DISPLACEMENT, "memory displacement mismatch");
    check(mem.memory_data_size() == mnos::DataSize::QWORD, "memory data size mismatch");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                mnos::Operand::mem(TEST_INVALID_REGISTER_ID, TEST_MEMORY_DISPLACEMENT, mnos::DataSize::QWORD));
        },
        "invalid memory base register");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                mnos::Operand::mem(mnos::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, TEST_INVALID_DATA_SIZE));
        },
        "invalid memory data size");
}

void test_instructions()
{
    const mnos::Instruction halt = mnos::Instruction::make_halt();
    check(halt.opcode() == mnos::Opcode::HALT, "HALT opcode mismatch");
    check(halt.first_operand().is_none(), "HALT first operand should be none");
    check(halt.second_operand().is_none(), "HALT second operand should be none");

    const mnos::Instruction mov =
        mnos::Instruction::make_mov(mnos::Operand::reg(mnos::RegisterId::RAX), mnos::Operand::imm(TEST_IMMEDIATE_VALUE));
    check(mov.opcode() == mnos::Opcode::MOV, "MOV opcode mismatch");
    check(mov.first_operand().register_id() == mnos::RegisterId::RAX, "MOV destination mismatch");
    check(mov.second_operand().immediate_value() == TEST_IMMEDIATE_VALUE, "MOV source mismatch");

    const mnos::Instruction add =
        mnos::Instruction::make_add(mnos::Operand::reg(mnos::RegisterId::RAX), mnos::Operand::reg(mnos::RegisterId::RBX));
    check(add.opcode() == mnos::Opcode::ADD, "ADD opcode mismatch");
    check(add.second_operand().register_id() == mnos::RegisterId::RBX, "ADD source mismatch");

    const mnos::Instruction sub =
        mnos::Instruction::make_sub(mnos::Operand::reg(mnos::RegisterId::RAX), mnos::Operand::imm(TEST_IMMEDIATE_VALUE));
    check(sub.opcode() == mnos::Opcode::SUB, "SUB opcode mismatch");

    const mnos::Instruction cmp =
        mnos::Instruction::make_cmp(mnos::Operand::reg(mnos::RegisterId::RAX), mnos::Operand::reg(mnos::RegisterId::RBX));
    check(cmp.opcode() == mnos::Opcode::CMP, "CMP opcode mismatch");

    const mnos::Instruction jump = mnos::Instruction::make_jmp(mnos::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump.opcode() == mnos::Opcode::JMP, "JMP opcode mismatch");
    check(jump.first_operand().immediate_value() == TEST_MEMORY_DISPLACEMENT, "JMP target mismatch");
    check(jump.second_operand().is_none(), "JMP second operand should be none");

    const mnos::Instruction jump_equal = mnos::Instruction::make_je(mnos::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump_equal.opcode() == mnos::Opcode::JE, "JE opcode mismatch");

    const mnos::Instruction jump_not_equal = mnos::Instruction::make_jne(mnos::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump_not_equal.opcode() == mnos::Opcode::JNE, "JNE opcode mismatch");
}
}

int main()
{
    test_data_size();
    test_register_ids_and_bank();
    test_flags();
    test_opcodes();
    test_operands();
    test_instructions();

    std::cout << "mnos_cpu tests passed\n";
    return 0;
}
