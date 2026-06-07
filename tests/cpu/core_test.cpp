#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/flags/rflags.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/opcode.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/register/bank.hpp>
#include <mnos/cpu/register/id.hpp>

namespace cpu = mnos::cpu;

namespace
{
constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);
constexpr auto TEST_INVALID_REGISTER_ID = static_cast<cpu::RegisterId>(cpu::REGISTER_ID_GENERAL_REGISTER_COUNT);
constexpr auto TEST_INVALID_FLAG_ID = static_cast<cpu::FlagId>(cpu::FLAG_ID_STATUS_FLAG_COUNT);
constexpr auto TEST_INVALID_OPCODE = static_cast<cpu::Opcode>(cpu::OPCODE_INSTRUCTION_KIND_COUNT);
constexpr auto TEST_INVALID_OPERAND_KIND = static_cast<cpu::OperandKind>(cpu::OPERAND_KIND_COUNT);

constexpr cpu::UQWORD64 TEST_REGISTER_VALUE = cpu::UQWORD64{0x1234ABCDULL};
constexpr cpu::UQWORD64 TEST_SECOND_REGISTER_VALUE = cpu::UQWORD64{0xFEDCBA98ULL};
constexpr cpu::SQWORD64 TEST_IMMEDIATE_VALUE = cpu::SQWORD64{-42};
constexpr cpu::SQWORD64 TEST_MEMORY_DISPLACEMENT = cpu::SQWORD64{16};
constexpr cpu::RIP64 TEST_FIRST_RIP = cpu::RIP64{0};
constexpr cpu::RIP64 TEST_SECOND_RIP = cpu::RIP64{1};
constexpr cpu::RIP64 TEST_TWO_INSTRUCTION_END_RIP = cpu::RIP64{2};
constexpr cpu::SQWORD64 TEST_BRANCH_TARGET = cpu::SQWORD64{4};
constexpr cpu::SQWORD64 TEST_JUMP_END_TARGET = cpu::SQWORD64{5};
constexpr cpu::SQWORD64 TEST_LOOP_TARGET = cpu::SQWORD64{0};
constexpr cpu::UQWORD64 TEST_ONE_BIT = cpu::UQWORD64{1};
constexpr cpu::UQWORD64 TEST_QWORD_SIGN_MASK =
    TEST_ONE_BIT << (cpu::DATA_SIZE_QWORD_BITS - std::size_t{1});
constexpr cpu::UQWORD64 TEST_CF_MASK = TEST_ONE_BIT << cpu::FLAG_ID_CF_BIT_INDEX;
constexpr cpu::SQWORD64 TEST_PROGRAM_INITIAL_VALUE = cpu::SQWORD64{1};
constexpr cpu::SQWORD64 TEST_LINEAR_ADD_VALUE = cpu::SQWORD64{2};
constexpr cpu::SQWORD64 TEST_LINEAR_SUB_VALUE = cpu::SQWORD64{1};
constexpr cpu::UQWORD64 TEST_LINEAR_EXPECTED_RAX = cpu::UQWORD64{2};
constexpr cpu::SQWORD64 TEST_EXECUTOR_EXPECTED_VALUE = cpu::SQWORD64{42};
constexpr cpu::SQWORD64 TEST_SKIPPED_BRANCH_VALUE = cpu::SQWORD64{13};
constexpr cpu::SQWORD64 TEST_FALLTHROUGH_BRANCH_VALUE = cpu::SQWORD64{7};
constexpr cpu::SQWORD64 TEST_SIGNED_QWORD_MAX = std::numeric_limits<cpu::SQWORD64>::max();
constexpr cpu::SQWORD64 TEST_ADD_CARRY_LEFT_VALUE = cpu::SQWORD64{-1};
constexpr cpu::SQWORD64 TEST_ADD_CARRY_RIGHT_VALUE = cpu::SQWORD64{1};
constexpr cpu::SQWORD64 TEST_SUB_BORROW_LEFT_VALUE = cpu::SQWORD64{0};
constexpr cpu::SQWORD64 TEST_SUB_BORROW_RIGHT_VALUE = cpu::SQWORD64{1};
constexpr std::size_t TEST_PROGRAM_RESERVE_COUNT = 8;
constexpr std::size_t TEST_SINGLE_INSTRUCTION_COUNT = 1;
constexpr std::size_t TEST_TWO_INSTRUCTION_COUNT = 2;
constexpr std::size_t TEST_LINEAR_PROGRAM_STEP_COUNT = 4;
constexpr std::size_t TEST_BRANCH_PROGRAM_STEP_COUNT = 5;
constexpr std::size_t TEST_LOOP_MAX_STEPS = 3;
constexpr cpu::RIP64 TEST_BRANCH_PROGRAM_FINAL_RIP = cpu::RIP64{6};
constexpr cpu::UQWORD64 TEST_TRACE_FIRST_CYCLE = cpu::UQWORD64{1};
constexpr bool TEST_TRACE_NOT_HALTED = false;

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

[[nodiscard]] cpu::Instruction make_mov_imm(const cpu::RegisterId destination, const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_mov(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] cpu::Instruction make_add_imm(const cpu::RegisterId destination, const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_add(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] cpu::Instruction make_sub_imm(const cpu::RegisterId destination, const cpu::SQWORD64 value)
{
    return cpu::Instruction::make_sub(cpu::Operand::reg(destination), cpu::Operand::imm(value));
}

[[nodiscard]] cpu::Instruction make_cmp_imm(const cpu::RegisterId left, const cpu::SQWORD64 right)
{
    return cpu::Instruction::make_cmp(cpu::Operand::reg(left), cpu::Operand::imm(right));
}

[[nodiscard]] cpu::Instruction make_jump_imm(const cpu::Opcode opcode, const cpu::SQWORD64 target)
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
    case cpu::Opcode::HALT:
    case cpu::Opcode::COUNT:
        throw std::logic_error{"test helper requires a jump opcode"};
    }
    throw std::logic_error{"test helper received unknown opcode"};
}

void test_data_size()
{
    struct DataSizeCase
    {
        cpu::DataSize size;
        std::size_t bits;
        std::size_t bytes;
        std::string_view assembly_name;
    };

    constexpr std::array<DataSizeCase, cpu::DATA_SIZE_KIND_COUNT> DATA_SIZE_CASES{
        DataSizeCase{cpu::DataSize::BYTE, cpu::DATA_SIZE_BYTE_BITS, cpu::DATA_SIZE_BYTE_BYTES, "BYTE"},
        DataSizeCase{cpu::DataSize::WORD, cpu::DATA_SIZE_WORD_BITS, cpu::DATA_SIZE_WORD_BYTES, "WORD"},
        DataSizeCase{cpu::DataSize::DWORD, cpu::DATA_SIZE_DWORD_BITS, cpu::DATA_SIZE_DWORD_BYTES, "DWORD"},
        DataSizeCase{cpu::DataSize::QWORD, cpu::DATA_SIZE_QWORD_BITS, cpu::DATA_SIZE_QWORD_BYTES, "QWORD"}};

    for (const DataSizeCase test_case : DATA_SIZE_CASES)
    {
        check(cpu::is_data_size_valid(test_case.size), "data size should be valid");
        check(cpu::data_size_to_bits(test_case.size) == test_case.bits, "data size bits mismatch");
        check(cpu::data_size_to_bytes(test_case.size) == test_case.bytes, "data size bytes mismatch");
        check(cpu::data_size_to_assembly_name(test_case.size) == test_case.assembly_name,
              "data size assembly name mismatch");
    }

    check(!cpu::is_data_size_valid(TEST_INVALID_DATA_SIZE), "invalid data size should be rejected");
    check(cpu::data_size_to_assembly_name(TEST_INVALID_DATA_SIZE) == "<invalid>",
          "invalid data size name should be stable");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::data_size_to_bits(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bits access");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::data_size_to_bytes(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bytes access");
}

void test_register_ids_and_bank()
{
    struct RegisterCase
    {
        cpu::RegisterId id;
        std::size_t index;
        std::string_view assembly_name;
    };

    constexpr std::array<RegisterCase, cpu::REGISTER_ID_GENERAL_REGISTER_COUNT> REGISTER_CASES{
        RegisterCase{cpu::RegisterId::RAX, 0, "RAX"},   RegisterCase{cpu::RegisterId::RBX, 1, "RBX"},
        RegisterCase{cpu::RegisterId::RCX, 2, "RCX"},   RegisterCase{cpu::RegisterId::RDX, 3, "RDX"},
        RegisterCase{cpu::RegisterId::RSI, 4, "RSI"},   RegisterCase{cpu::RegisterId::RDI, 5, "RDI"},
        RegisterCase{cpu::RegisterId::RBP, 6, "RBP"},   RegisterCase{cpu::RegisterId::RSP, 7, "RSP"},
        RegisterCase{cpu::RegisterId::R8, 8, "R8"},     RegisterCase{cpu::RegisterId::R9, 9, "R9"},
        RegisterCase{cpu::RegisterId::R10, 10, "R10"},  RegisterCase{cpu::RegisterId::R11, 11, "R11"},
        RegisterCase{cpu::RegisterId::R12, 12, "R12"},  RegisterCase{cpu::RegisterId::R13, 13, "R13"},
        RegisterCase{cpu::RegisterId::R14, 14, "R14"},  RegisterCase{cpu::RegisterId::R15, 15, "R15"}};

    for (const RegisterCase test_case : REGISTER_CASES)
    {
        check(cpu::is_register_id_valid(test_case.id), "register id should be valid");
        check(cpu::register_id_to_index(test_case.id) == test_case.index, "register index mismatch");
        check(cpu::register_id_to_assembly_name(test_case.id) == test_case.assembly_name,
              "register assembly name mismatch");
    }

    check(!cpu::is_register_id_valid(TEST_INVALID_REGISTER_ID), "invalid register id should be rejected");
    check(cpu::register_id_to_assembly_name(TEST_INVALID_REGISTER_ID) == "<invalid>",
          "invalid register name should be stable");

    cpu::RegisterBank bank;
    check(bank.read(cpu::RegisterId::RAX) == cpu::UQWORD64{0}, "register bank should zero initialize");
    bank.write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    bank.write(cpu::RegisterId::R15, TEST_SECOND_REGISTER_VALUE);
    check(bank.read(cpu::RegisterId::RAX) == TEST_REGISTER_VALUE, "register bank RAX read mismatch");
    check(bank.read(cpu::RegisterId::R15) == TEST_SECOND_REGISTER_VALUE, "register bank R15 read mismatch");
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
        cpu::FlagId id;
        std::size_t index;
        std::size_t bit_index;
        std::string_view assembly_name;
    };

    constexpr std::array<FlagCase, cpu::FLAG_ID_STATUS_FLAG_COUNT> FLAG_CASES{
        FlagCase{cpu::FlagId::CF, 0, cpu::FLAG_ID_CF_BIT_INDEX, "CF"},
        FlagCase{cpu::FlagId::ZF, 1, cpu::FLAG_ID_ZF_BIT_INDEX, "ZF"},
        FlagCase{cpu::FlagId::SF, 2, cpu::FLAG_ID_SF_BIT_INDEX, "SF"},
        FlagCase{cpu::FlagId::OF, 3, cpu::FLAG_ID_OF_BIT_INDEX, "OF"}};

    for (const FlagCase test_case : FLAG_CASES)
    {
        check(cpu::is_flag_id_valid(test_case.id), "flag id should be valid");
        check(cpu::flag_id_to_index(test_case.id) == test_case.index, "flag index mismatch");
        check(cpu::flag_id_to_bit_index(test_case.id) == test_case.bit_index, "flag bit index mismatch");
        check(cpu::flag_id_to_assembly_name(test_case.id) == test_case.assembly_name,
              "flag assembly name mismatch");
    }

    check(!cpu::is_flag_id_valid(TEST_INVALID_FLAG_ID), "invalid flag id should be rejected");
    check(cpu::flag_id_to_assembly_name(TEST_INVALID_FLAG_ID) == "<invalid>",
          "invalid flag name should be stable");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::flag_id_to_bit_index(TEST_INVALID_FLAG_ID));
        },
        "invalid flag bit index");

    cpu::Rflags flags;
    check(flags.raw_bits() == cpu::UQWORD64{0}, "rflags should zero initialize");
    flags.write(cpu::FlagId::CF, true);
    check(flags.read(cpu::FlagId::CF), "CF should be set");
    check(flags.raw_bits() == TEST_CF_MASK, "CF raw bit mismatch");
    flags.write(cpu::FlagId::CF, false);
    check(!flags.read(cpu::FlagId::CF), "CF should be cleared");

    flags.update_zero_sign_from_qword(cpu::UQWORD64{0});
    check(flags.read(cpu::FlagId::ZF), "zero result should set ZF");
    check(!flags.read(cpu::FlagId::SF), "zero result should clear SF");

    flags.update_zero_sign_from_qword(TEST_QWORD_SIGN_MASK);
    check(!flags.read(cpu::FlagId::ZF), "non-zero result should clear ZF");
    check(flags.read(cpu::FlagId::SF), "sign bit should set SF");

    flags.clear_status_flags();
    check(flags.raw_bits() == cpu::UQWORD64{0}, "clear status flags should clear raw bits");
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
        check(cpu::is_opcode_valid(test_case.opcode), "opcode should be valid");
        check(cpu::opcode_to_index(test_case.opcode) == test_case.index, "opcode index mismatch");
        check(cpu::opcode_to_assembly_name(test_case.opcode) == test_case.assembly_name,
              "opcode assembly name mismatch");
    }

    check(!cpu::is_opcode_valid(TEST_INVALID_OPCODE), "invalid opcode should be rejected");
    check(cpu::opcode_to_assembly_name(TEST_INVALID_OPCODE) == "<invalid>",
          "invalid opcode name should be stable");
}

void test_operands()
{
    check(cpu::is_operand_kind_valid(cpu::OperandKind::MEMORY), "memory operand kind should be valid");
    check(!cpu::is_operand_kind_valid(TEST_INVALID_OPERAND_KIND), "invalid operand kind should be rejected");
    check(cpu::operand_kind_to_index(cpu::OperandKind::IMMEDIATE) ==
              static_cast<std::size_t>(cpu::OperandKind::IMMEDIATE),
          "operand kind index mismatch");
    check(cpu::operand_kind_to_assembly_name(cpu::OperandKind::NONE) == "NONE", "NONE operand name mismatch");
    check(cpu::operand_kind_to_assembly_name(TEST_INVALID_OPERAND_KIND) == "<invalid>",
          "invalid operand kind name should be stable");

    const cpu::Operand none = cpu::Operand::none();
    check(none.kind() == cpu::OperandKind::NONE, "none operand kind mismatch");
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

    const cpu::Operand reg = cpu::Operand::reg(cpu::RegisterId::RCX);
    check(reg.kind() == cpu::OperandKind::REGISTER, "register operand kind mismatch");
    check(reg.is_register(), "register operand predicate mismatch");
    check(reg.register_id() == cpu::RegisterId::RCX, "register operand payload mismatch");
    check_throws<std::logic_error>(
        [&reg]() {
            static_cast<void>(reg.memory_data_size());
        },
        "register operand memory data size access");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::Operand::reg(TEST_INVALID_REGISTER_ID));
        },
        "invalid register operand creation");

    const cpu::Operand imm = cpu::Operand::imm(TEST_IMMEDIATE_VALUE);
    check(imm.kind() == cpu::OperandKind::IMMEDIATE, "immediate operand kind mismatch");
    check(imm.is_immediate(), "immediate operand predicate mismatch");
    check(imm.immediate_value() == TEST_IMMEDIATE_VALUE, "immediate payload mismatch");
    check_throws<std::logic_error>(
        [&imm]() {
            static_cast<void>(imm.memory_base_register());
        },
        "immediate operand memory access");

    const cpu::Operand mem =
        cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD);
    check(mem.kind() == cpu::OperandKind::MEMORY, "memory operand kind mismatch");
    check(mem.is_memory(), "memory operand predicate mismatch");
    check(mem.memory_base_register() == cpu::RegisterId::RBP, "memory base register mismatch");
    check(mem.memory_displacement() == TEST_MEMORY_DISPLACEMENT, "memory displacement mismatch");
    check(mem.memory_data_size() == cpu::DataSize::QWORD, "memory data size mismatch");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                cpu::Operand::mem(TEST_INVALID_REGISTER_ID, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD));
        },
        "invalid memory base register");
    check_throws<std::out_of_range>(
        []() {
            static_cast<void>(
                cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, TEST_INVALID_DATA_SIZE));
        },
        "invalid memory data size");
}

void test_instructions()
{
    const cpu::Instruction halt = cpu::Instruction::make_halt();
    check(halt.opcode() == cpu::Opcode::HALT, "HALT opcode mismatch");
    check(halt.first_operand().is_none(), "HALT first operand should be none");
    check(halt.second_operand().is_none(), "HALT second operand should be none");

    const cpu::Instruction mov =
        cpu::Instruction::make_mov(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    check(mov.opcode() == cpu::Opcode::MOV, "MOV opcode mismatch");
    check(mov.first_operand().register_id() == cpu::RegisterId::RAX, "MOV destination mismatch");
    check(mov.second_operand().immediate_value() == TEST_IMMEDIATE_VALUE, "MOV source mismatch");

    const cpu::Instruction add =
        cpu::Instruction::make_add(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    check(add.opcode() == cpu::Opcode::ADD, "ADD opcode mismatch");
    check(add.second_operand().register_id() == cpu::RegisterId::RBX, "ADD source mismatch");

    const cpu::Instruction sub =
        cpu::Instruction::make_sub(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_IMMEDIATE_VALUE));
    check(sub.opcode() == cpu::Opcode::SUB, "SUB opcode mismatch");

    const cpu::Instruction cmp =
        cpu::Instruction::make_cmp(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::reg(cpu::RegisterId::RBX));
    check(cmp.opcode() == cpu::Opcode::CMP, "CMP opcode mismatch");

    const cpu::Instruction jump = cpu::Instruction::make_jmp(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump.opcode() == cpu::Opcode::JMP, "JMP opcode mismatch");
    check(jump.first_operand().immediate_value() == TEST_MEMORY_DISPLACEMENT, "JMP target mismatch");
    check(jump.second_operand().is_none(), "JMP second operand should be none");

    const cpu::Instruction jump_equal = cpu::Instruction::make_je(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump_equal.opcode() == cpu::Opcode::JE, "JE opcode mismatch");

    const cpu::Instruction jump_not_equal = cpu::Instruction::make_jne(cpu::Operand::imm(TEST_MEMORY_DISPLACEMENT));
    check(jump_not_equal.opcode() == cpu::Opcode::JNE, "JNE opcode mismatch");
}

void test_cpu_state()
{
    cpu::CpuState state;
    const cpu::CpuState& const_state = state;
    check(state.rip() == cpu::CPU_STATE_INITIAL_RIP, "cpu state initial RIP mismatch");
    check(!state.is_halted(), "cpu state should not start halted");
    state.registers().write(cpu::RegisterId::RAX, TEST_REGISTER_VALUE);
    state.flags().write(cpu::FlagId::ZF, true);
    state.advance_rip();
    state.halt();
    check(const_state.registers().read(cpu::RegisterId::RAX) == TEST_REGISTER_VALUE, "const registers view mismatch");
    check(const_state.flags().read(cpu::FlagId::ZF), "const flags view mismatch");
    check(state.rip() == cpu::CPU_STATE_NEXT_INSTRUCTION_OFFSET, "cpu state advance RIP mismatch");
    check(state.is_halted(), "cpu state halt mismatch");
    state.resume();
    check(!state.is_halted(), "cpu state resume mismatch");
    state.reset();
    check(state.rip() == cpu::CPU_STATE_INITIAL_RIP, "cpu state reset RIP mismatch");
    check(state.registers().read(cpu::RegisterId::RAX) == cpu::UQWORD64{0}, "cpu state reset registers mismatch");
    check(!state.flags().read(cpu::FlagId::ZF), "cpu state reset flags mismatch");
}

void test_program_container()
{
    cpu::Program program;
    check(program.empty(), "program should start empty");
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu::Instruction::make_halt());
    check(!program.empty(), "program should not be empty after push");
    check(program.size() == TEST_TWO_INSTRUCTION_COUNT, "program size mismatch");
    check(program.contains_rip(TEST_FIRST_RIP), "program should contain RIP 0");
    check(!program.contains_rip(TEST_TWO_INSTRUCTION_END_RIP), "program should reject end RIP");
    check(program.at(0).opcode() == cpu::Opcode::MOV, "program at mismatch");
    check(program.instruction_at(TEST_SECOND_RIP).opcode() == cpu::Opcode::HALT, "program instruction_at mismatch");
    check(program.instructions().size() == program.size(), "program span size mismatch");
    check(program.begin()->opcode() == cpu::Opcode::MOV, "program begin mismatch");
    auto program_iterator = program.begin();
    ++program_iterator;
    ++program_iterator;
    check(program_iterator == program.end(), "program end mismatch");
    check_throws<std::out_of_range>(
        [&program]() {
            static_cast<void>(program.instruction_at(TEST_TWO_INSTRUCTION_END_RIP));
        },
        "program invalid RIP");
    program.clear();
    check(program.empty(), "program clear mismatch");

    const cpu::Program initialized_program{
        cpu::Instruction::make_halt(),
    };
    check(initialized_program.size() == TEST_SINGLE_INSTRUCTION_COUNT, "initializer-list program size mismatch");

    std::vector<cpu::Instruction> raw_instructions;
    raw_instructions.push_back(cpu::Instruction::make_halt());
    const cpu::Program vector_program{std::move(raw_instructions)};
    check(vector_program.size() == TEST_SINGLE_INSTRUCTION_COUNT, "vector program size mismatch");
    check(vector_program.begin()->opcode() == cpu::Opcode::HALT, "vector program opcode mismatch");
}

void test_execution_trace_container()
{
    cpu::ExecutionTrace trace;
    check(trace.empty(), "trace should start empty");
    trace.reserve(TEST_PROGRAM_RESERVE_COUNT);
    trace.push_back(cpu::ExecutionTraceEntry{
        TEST_TRACE_FIRST_CYCLE,
        TEST_FIRST_RIP,
        TEST_SECOND_RIP,
        cpu::Opcode::MOV,
        TEST_TRACE_NOT_HALTED});
    check(!trace.empty(), "trace should not be empty after push");
    check(trace.size() == TEST_SINGLE_INSTRUCTION_COUNT, "trace size mismatch");
    check(trace.at(0).opcode == cpu::Opcode::MOV, "trace at mismatch");
    check(trace.entries().front().rip_after == TEST_SECOND_RIP, "trace span mismatch");
    check(trace.begin()->cycle == TEST_TRACE_FIRST_CYCLE, "trace begin mismatch");
    auto trace_iterator = trace.begin();
    ++trace_iterator;
    check(trace_iterator == trace.end(), "trace end mismatch");
    trace.clear();
    check(trace.empty(), "trace clear mismatch");
}

void test_executor_linear_program()
{
    cpu::Program program;
    program.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(make_add_imm(cpu::RegisterId::RAX, TEST_LINEAR_ADD_VALUE));
    program.push_back(make_sub_imm(cpu::RegisterId::RAX, TEST_LINEAR_SUB_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    const std::size_t executed_steps = executor.run(state, program, cpu::EXECUTOR_DEFAULT_MAX_STEPS, &trace);

    check(executed_steps == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program executed step count mismatch");
    check(executor.cycle_count() == TEST_LINEAR_PROGRAM_STEP_COUNT, "executor cycle count mismatch");
    check(state.is_halted(), "linear program should halt");
    check(state.rip() == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program final RIP mismatch");
    check(state.registers().read(cpu::RegisterId::RAX) == TEST_LINEAR_EXPECTED_RAX, "linear program RAX mismatch");
    check(trace.size() == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program trace size mismatch");
    check(trace.at(0).rip_before == TEST_FIRST_RIP, "linear trace first RIP mismatch");
    check(trace.at(0).opcode == cpu::Opcode::MOV, "linear trace first opcode mismatch");
    check(trace.at(TEST_LINEAR_PROGRAM_STEP_COUNT - std::size_t{1}).halted_after, "linear trace halt mismatch");

    executor.reset();
    check(executor.cycle_count() == cpu::UQWORD64{0}, "executor reset mismatch");
    check(executor.step(state, program) == cpu::StepResult::HALTED, "step on halted state mismatch");
}

void test_executor_branch_program()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(make_jump_imm(cpu::Opcode::JE, TEST_BRANCH_TARGET));
    program.push_back(make_mov_imm(cpu::RegisterId::RBX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    check(executed_steps == TEST_BRANCH_PROGRAM_STEP_COUNT, "branch program executed step count mismatch");
    check(state.registers().read(cpu::RegisterId::RBX) == static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE),
          "branch program RBX mismatch");
    check(state.flags().read(cpu::FlagId::ZF), "branch program ZF mismatch");
    check(state.rip() == TEST_BRANCH_PROGRAM_FINAL_RIP, "branch program final RIP mismatch");
}

void test_executor_jne_fallthrough()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(make_jump_imm(cpu::Opcode::JNE, TEST_BRANCH_TARGET));
    program.push_back(make_mov_imm(cpu::RegisterId::RBX, TEST_FALLTHROUGH_BRANCH_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    check(state.registers().read(cpu::RegisterId::RBX) == static_cast<cpu::UQWORD64>(TEST_FALLTHROUGH_BRANCH_VALUE),
          "JNE fallthrough RBX mismatch");
    check(state.flags().read(cpu::FlagId::ZF), "JNE fallthrough ZF mismatch");
}

void test_executor_unconditional_jump()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(make_jump_imm(cpu::Opcode::JMP, TEST_BRANCH_TARGET));
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(make_jump_imm(cpu::Opcode::JMP, TEST_JUMP_END_TARGET));
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    check(state.registers().read(cpu::RegisterId::RAX) == static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE),
          "JMP program RAX mismatch");
}

void test_executor_arithmetic_flags()
{
    cpu::Program carry_program;
    carry_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    carry_program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_LEFT_VALUE));
    carry_program.push_back(make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    carry_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState carry_state;
    cpu::Executor executor;
    static_cast<void>(executor.run(carry_state, carry_program));
    check(carry_state.registers().read(cpu::RegisterId::RAX) == cpu::UQWORD64{0}, "ADD carry result mismatch");
    check(carry_state.flags().read(cpu::FlagId::CF), "ADD carry CF mismatch");
    check(carry_state.flags().read(cpu::FlagId::ZF), "ADD carry ZF mismatch");

    cpu::Program overflow_program;
    overflow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    overflow_program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_SIGNED_QWORD_MAX));
    overflow_program.push_back(make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    overflow_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState overflow_state;
    executor.reset();
    static_cast<void>(executor.run(overflow_state, overflow_program));
    check(overflow_state.flags().read(cpu::FlagId::OF), "ADD overflow OF mismatch");
    check(overflow_state.flags().read(cpu::FlagId::SF), "ADD overflow SF mismatch");

    cpu::Program borrow_program;
    borrow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    borrow_program.push_back(make_mov_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_LEFT_VALUE));
    borrow_program.push_back(make_sub_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_RIGHT_VALUE));
    borrow_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState borrow_state;
    executor.reset();
    static_cast<void>(executor.run(borrow_state, borrow_program));
    check(borrow_state.flags().read(cpu::FlagId::CF), "SUB borrow CF mismatch");
    check(borrow_state.flags().read(cpu::FlagId::SF), "SUB borrow SF mismatch");
}

void test_executor_error_paths()
{
    cpu::Program invalid_jump_program{
        make_jump_imm(cpu::Opcode::JMP, TEST_MEMORY_DISPLACEMENT),
    };
    cpu::CpuState invalid_jump_state;
    cpu::Executor executor;
    check_throws<std::out_of_range>(
        [&executor, &invalid_jump_state, &invalid_jump_program]() {
            static_cast<void>(executor.step(invalid_jump_state, invalid_jump_program));
        },
        "invalid jump target");

    cpu::Program memory_source_program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
    };
    cpu::CpuState memory_source_state;
    executor.reset();
    check_throws<std::logic_error>(
        [&executor, &memory_source_state, &memory_source_program]() {
            static_cast<void>(executor.step(memory_source_state, memory_source_program));
        },
        "unsupported memory operand");

    cpu::Program non_register_destination_program{
        cpu::Instruction::make_mov(cpu::Operand::imm(TEST_IMMEDIATE_VALUE), cpu::Operand::imm(TEST_IMMEDIATE_VALUE)),
    };
    cpu::CpuState non_register_destination_state;
    executor.reset();
    check_throws<std::logic_error>(
        [&executor, &non_register_destination_state, &non_register_destination_program]() {
            static_cast<void>(executor.step(non_register_destination_state, non_register_destination_program));
        },
        "non-register destination");

    cpu::Program none_operand_program{
        cpu::Instruction::make_add(cpu::Operand::none(), cpu::Operand::imm(TEST_PROGRAM_INITIAL_VALUE)),
    };
    cpu::CpuState none_operand_state;
    executor.reset();
    check_throws<std::logic_error>(
        [&executor, &none_operand_state, &none_operand_program]() {
            static_cast<void>(executor.step(none_operand_state, none_operand_program));
        },
        "none operand read");

    cpu::Program loop_program{
        make_jump_imm(cpu::Opcode::JMP, TEST_LOOP_TARGET),
    };
    cpu::CpuState loop_state;
    executor.reset();
    check_throws<std::runtime_error>(
        [&executor, &loop_state, &loop_program]() {
            static_cast<void>(executor.run(loop_state, loop_program, TEST_LOOP_MAX_STEPS));
        },
        "executor max steps");
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
    test_cpu_state();
    test_program_container();
    test_execution_trace_container();
    test_executor_linear_program();
    test_executor_branch_program();
    test_executor_jne_fallthrough();
    test_executor_unconditional_jump();
    test_executor_arithmetic_flags();
    test_executor_error_paths();

    std::cout << "mnos_cpu tests passed\n";
    return 0;
}
