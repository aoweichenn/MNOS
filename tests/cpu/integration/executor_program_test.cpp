#include <iostream>
#include <limits>
#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <support/test_assert.hpp>
#include <cpu/support/cpu_test_helpers.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
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
constexpr std::size_t TEST_LINEAR_PROGRAM_STEP_COUNT = 4;
constexpr std::size_t TEST_BRANCH_PROGRAM_STEP_COUNT = 5;
constexpr std::size_t TEST_LOOP_MAX_STEPS = 3;
constexpr cpu::SQWORD64 TEST_BRANCH_TARGET = cpu::SQWORD64{4};
constexpr cpu::SQWORD64 TEST_JUMP_END_TARGET = cpu::SQWORD64{5};
constexpr cpu::SQWORD64 TEST_LOOP_TARGET = cpu::SQWORD64{0};
constexpr cpu::RIP64 TEST_FIRST_RIP = cpu::RIP64{0};
constexpr cpu::RIP64 TEST_BRANCH_PROGRAM_FINAL_RIP = cpu::RIP64{6};

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::ADDRESS64 TEST_MEMORY_BASE_ADDRESS = cpu::ADDRESS64{16};
constexpr cpu::ADDRESS64 TEST_MEMORY_SECOND_ADDRESS = cpu::ADDRESS64{24};
constexpr cpu::ADDRESS64 TEST_MEMORY_EFFECTIVE_ADDRESS = cpu::ADDRESS64{32};
constexpr cpu::SQWORD64 TEST_MEMORY_POSITIVE_DISPLACEMENT = cpu::SQWORD64{16};
constexpr cpu::SQWORD64 TEST_MEMORY_NEGATIVE_BASE = cpu::SQWORD64{32};
constexpr cpu::SQWORD64 TEST_MEMORY_NEGATIVE_DISPLACEMENT = cpu::SQWORD64{-8};
constexpr cpu::SQWORD64 TEST_MEMORY_EXECUTOR_VALUE = cpu::SQWORD64{0x123456789ABCDEF0LL};
constexpr cpu::SQWORD64 TEST_MEMORY_ADD_INITIAL_VALUE = cpu::SQWORD64{10};
constexpr cpu::SQWORD64 TEST_MEMORY_ADD_INCREMENT = cpu::SQWORD64{32};
constexpr cpu::SQWORD64 TEST_MEMORY_ADD_EXPECTED_VALUE = cpu::SQWORD64{42};
constexpr std::size_t TEST_MEMORY_MOV_PROGRAM_STEP_COUNT = 5;
constexpr std::size_t TEST_MEMORY_ARITHMETIC_PROGRAM_STEP_COUNT = 6;
constexpr std::size_t TEST_MEMORY_NEGATIVE_PROGRAM_STEP_COUNT = 4;
constexpr cpu::SQWORD64 TEST_MEMORY_BRANCH_TARGET = cpu::SQWORD64{5};

void test_executor_linear_program()
{
    cpu::Program program;
    program.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_LINEAR_ADD_VALUE));
    program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::RAX, TEST_LINEAR_SUB_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    const std::size_t executed_steps = executor.run(state, program, cpu::EXECUTOR_DEFAULT_MAX_STEPS, &trace);

    test::check(executed_steps == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program executed step count mismatch");
    test::check(executor.cycle_count() == TEST_LINEAR_PROGRAM_STEP_COUNT, "executor cycle count mismatch");
    test::check(state.is_halted(), "linear program should halt");
    test::check(state.rip() == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program final RIP mismatch");
    test::check(state.registers().read(cpu::RegisterId::RAX) == TEST_LINEAR_EXPECTED_RAX,
                "linear program RAX mismatch");
    test::check(trace.size() == TEST_LINEAR_PROGRAM_STEP_COUNT, "linear program trace size mismatch");
    test::check(trace.at(0).rip_before == TEST_FIRST_RIP, "linear trace first RIP mismatch");
    test::check(trace.at(0).opcode == cpu::Opcode::MOV, "linear trace first opcode mismatch");
    test::check(trace.at(TEST_LINEAR_PROGRAM_STEP_COUNT - std::size_t{1}).halted_after,
                "linear trace halt mismatch");

    executor.reset();
    test::check(executor.cycle_count() == cpu::UQWORD64{0}, "executor reset mismatch");
    test::check(executor.step(state, program) == cpu::StepResult::HALTED, "step on halted state mismatch");
}

void test_executor_branch_program()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JE, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    test::check(executed_steps == TEST_BRANCH_PROGRAM_STEP_COUNT, "branch program executed step count mismatch");
    test::check(state.registers().read(cpu::RegisterId::RBX) == static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE),
                "branch program RBX mismatch");
    test::check(state.flags().read(cpu::FlagId::ZF), "branch program ZF mismatch");
    test::check(state.rip() == TEST_BRANCH_PROGRAM_FINAL_RIP, "branch program final RIP mismatch");
}

void test_executor_jne_fallthrough()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JNE, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_FALLTHROUGH_BRANCH_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    test::check(state.registers().read(cpu::RegisterId::RBX) ==
                    static_cast<cpu::UQWORD64>(TEST_FALLTHROUGH_BRANCH_VALUE),
                "JNE fallthrough RBX mismatch");
    test::check(state.flags().read(cpu::FlagId::ZF), "JNE fallthrough ZF mismatch");
}

void test_executor_unconditional_jump()
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_JUMP_END_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    test::check(state.registers().read(cpu::RegisterId::RAX) == static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE),
                "JMP program RAX mismatch");
}

void test_executor_arithmetic_flags()
{
    cpu::Program carry_program;
    carry_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    carry_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_LEFT_VALUE));
    carry_program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    carry_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState carry_state;
    cpu::Executor executor;
    static_cast<void>(executor.run(carry_state, carry_program));
    test::check(carry_state.registers().read(cpu::RegisterId::RAX) == cpu::UQWORD64{0}, "ADD carry result mismatch");
    test::check(carry_state.flags().read(cpu::FlagId::CF), "ADD carry CF mismatch");
    test::check(carry_state.flags().read(cpu::FlagId::ZF), "ADD carry ZF mismatch");

    cpu::Program overflow_program;
    overflow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    overflow_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SIGNED_QWORD_MAX));
    overflow_program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    overflow_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState overflow_state;
    executor.reset();
    static_cast<void>(executor.run(overflow_state, overflow_program));
    test::check(overflow_state.flags().read(cpu::FlagId::OF), "ADD overflow OF mismatch");
    test::check(overflow_state.flags().read(cpu::FlagId::SF), "ADD overflow SF mismatch");

    cpu::Program borrow_program;
    borrow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    borrow_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_LEFT_VALUE));
    borrow_program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_RIGHT_VALUE));
    borrow_program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState borrow_state;
    executor.reset();
    static_cast<void>(executor.run(borrow_state, borrow_program));
    test::check(borrow_state.flags().read(cpu::FlagId::CF), "SUB borrow CF mismatch");
    test::check(borrow_state.flags().read(cpu::FlagId::SF), "SUB borrow SF mismatch");
}

void test_executor_memory_mov_program()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    cpu::Program program;
    program.reserve(TEST_MEMORY_MOV_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(
        cpu::RegisterId::RBP,
        static_cast<cpu::SQWORD64>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_MEMORY_EXECUTOR_VALUE));
    program.push_back(cpu::Instruction::make_mov(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RBX),
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD)));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    test::check(executed_steps == TEST_MEMORY_MOV_PROGRAM_STEP_COUNT, "memory MOV step count mismatch");
    test::check(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS) ==
                    static_cast<cpu::UQWORD64>(TEST_MEMORY_EXECUTOR_VALUE),
                "memory MOV stored value mismatch");
    test::check(state.registers().read(cpu::RegisterId::RBX) ==
                    static_cast<cpu::UQWORD64>(TEST_MEMORY_EXECUTOR_VALUE),
                "memory MOV loaded register mismatch");
}

void test_executor_memory_arithmetic_program()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_MEMORY_EFFECTIVE_ADDRESS, static_cast<cpu::UQWORD64>(TEST_MEMORY_ADD_INITIAL_VALUE));

    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(
        cpu::RegisterId::RBP,
        static_cast<cpu::SQWORD64>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu::Instruction::make_add(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_INCREMENT)));
    program.push_back(cpu::Instruction::make_cmp(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_EXPECTED_VALUE)));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JE, TEST_MEMORY_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    test::check(executed_steps == TEST_MEMORY_ARITHMETIC_PROGRAM_STEP_COUNT,
                "memory arithmetic step count mismatch");
    test::check(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS) ==
                    static_cast<cpu::UQWORD64>(TEST_MEMORY_ADD_EXPECTED_VALUE),
                "memory arithmetic stored value mismatch");
    test::check(state.flags().read(cpu::FlagId::ZF), "memory arithmetic CMP should set ZF");
    test::check(state.registers().read(cpu::RegisterId::RAX) == static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE),
                "memory arithmetic branch result mismatch");
}

void test_executor_memory_negative_displacement()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    cpu::Program program;
    program.reserve(TEST_MEMORY_NEGATIVE_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, TEST_MEMORY_NEGATIVE_BASE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_MEMORY_ADD_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_mov(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_NEGATIVE_DISPLACEMENT, cpu::DataSize::DWORD),
        cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_halt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    test::check(executed_steps == TEST_MEMORY_NEGATIVE_PROGRAM_STEP_COUNT,
                "memory negative displacement step count mismatch");
    test::check(memory.read_dword(TEST_MEMORY_SECOND_ADDRESS) == static_cast<cpu::UDWORD32>(TEST_MEMORY_ADD_EXPECTED_VALUE),
                "memory negative displacement stored value mismatch");
}
}

int main()
{
    test_executor_linear_program();
    test_executor_branch_program();
    test_executor_jne_fallthrough();
    test_executor_unconditional_jump();
    test_executor_arithmetic_flags();
    test_executor_memory_mov_program();
    test_executor_memory_arithmetic_program();
    test_executor_memory_negative_displacement();

    std::cout << "mnos_cpu_executor_integration_tests passed\n";
    return 0;
}
