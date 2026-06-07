#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpu/support/cpu_test_helpers.hpp>
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

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
using ::testing::Eq;

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
constexpr cpu::SQWORD64 TEST_BRANCH_TARGET = cpu::SQWORD64{4};
constexpr cpu::SQWORD64 TEST_JUMP_END_TARGET = cpu::SQWORD64{5};
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
}

TEST(ExecutorProgramTest, RunsLinearProgramAndRecordsTrace)
{
    cpu::Program program;
    program.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_LINEAR_ADD_VALUE));
    program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::RAX, TEST_LINEAR_SUB_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_LINEAR_PROGRAM_STEP_COUNT);
    const std::size_t executed_steps = executor.run(state, program, cpu::EXECUTOR_DEFAULT_MAX_STEPS, &trace);

    EXPECT_THAT(executed_steps, Eq(TEST_LINEAR_PROGRAM_STEP_COUNT));
    EXPECT_THAT(executor.cycle_count(), Eq(TEST_LINEAR_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(state.rip(), Eq(TEST_LINEAR_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(TEST_LINEAR_EXPECTED_RAX));
    EXPECT_THAT(trace.size(), Eq(TEST_LINEAR_PROGRAM_STEP_COUNT));
    EXPECT_THAT(trace.at(0).rip_before, Eq(TEST_FIRST_RIP));
    EXPECT_THAT(trace.at(0).opcode, Eq(cpu::Opcode::MOV));
    EXPECT_TRUE(trace.at(TEST_LINEAR_PROGRAM_STEP_COUNT - std::size_t{1}).halted_after);

    executor.reset();
    EXPECT_THAT(executor.cycle_count(), Eq(cpu::UQWORD64{0}));
    EXPECT_THAT(executor.step(state, program), Eq(cpu::StepResult::HALTED));
}

TEST(ExecutorProgramTest, RunsConditionalBranchProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JE, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    EXPECT_THAT(executed_steps, Eq(TEST_BRANCH_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.rip(), Eq(TEST_BRANCH_PROGRAM_FINAL_RIP));
}

TEST(ExecutorProgramTest, LetsJneFallThroughWhenZeroFlagIsSet)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JNE, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_FALLTHROUGH_BRANCH_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::UQWORD64>(TEST_FALLTHROUGH_BRANCH_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
}

TEST(ExecutorProgramTest, RunsUnconditionalJumpProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_JUMP_END_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE)));
}

TEST(ExecutorProgramTest, UpdatesArithmeticFlags)
{
    cpu::Program carry_program;
    carry_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    carry_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_LEFT_VALUE));
    carry_program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    carry_program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState carry_state;
    cpu::Executor executor;
    static_cast<void>(executor.run(carry_state, carry_program));
    EXPECT_THAT(carry_state.registers().read(cpu::RegisterId::RAX), Eq(cpu::UQWORD64{0}));
    EXPECT_TRUE(carry_state.flags().read(cpu::FlagId::CF));
    EXPECT_TRUE(carry_state.flags().read(cpu::FlagId::ZF));

    cpu::Program overflow_program;
    overflow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    overflow_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SIGNED_QWORD_MAX));
    overflow_program.push_back(cpu_support::make_add_imm(cpu::RegisterId::RAX, TEST_ADD_CARRY_RIGHT_VALUE));
    overflow_program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState overflow_state;
    executor.reset();
    static_cast<void>(executor.run(overflow_state, overflow_program));
    EXPECT_TRUE(overflow_state.flags().read(cpu::FlagId::OF));
    EXPECT_TRUE(overflow_state.flags().read(cpu::FlagId::SF));

    cpu::Program borrow_program;
    borrow_program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    borrow_program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_LEFT_VALUE));
    borrow_program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::RAX, TEST_SUB_BORROW_RIGHT_VALUE));
    borrow_program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState borrow_state;
    executor.reset();
    static_cast<void>(executor.run(borrow_state, borrow_program));
    EXPECT_TRUE(borrow_state.flags().read(cpu::FlagId::CF));
    EXPECT_TRUE(borrow_state.flags().read(cpu::FlagId::SF));
}

TEST(ExecutorProgramTest, MovesValuesThroughMemory)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    cpu::Program program;
    program.reserve(TEST_MEMORY_MOV_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SQWORD64>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_MEMORY_EXECUTOR_VALUE));
    program.push_back(cpu::Instruction::make_mov(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RBX),
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD)));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_MEMORY_MOV_PROGRAM_STEP_COUNT));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS), Eq(static_cast<cpu::UQWORD64>(TEST_MEMORY_EXECUTOR_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::UQWORD64>(TEST_MEMORY_EXECUTOR_VALUE)));
}

TEST(ExecutorProgramTest, RunsArithmeticAgainstMemoryOperands)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_MEMORY_EFFECTIVE_ADDRESS, static_cast<cpu::UQWORD64>(TEST_MEMORY_ADD_INITIAL_VALUE));

    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SQWORD64>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu::Instruction::make_add(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_INCREMENT)));
    program.push_back(cpu::Instruction::make_cmp(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_EXPECTED_VALUE)));
    program.push_back(cpu_support::make_jump_imm(cpu::Opcode::JE, TEST_MEMORY_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_MEMORY_ARITHMETIC_PROGRAM_STEP_COUNT));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS), Eq(static_cast<cpu::UQWORD64>(TEST_MEMORY_ADD_EXPECTED_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::UQWORD64>(TEST_EXECUTOR_EXPECTED_VALUE)));
}

TEST(ExecutorProgramTest, SupportsNegativeMemoryDisplacement)
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
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_MEMORY_NEGATIVE_PROGRAM_STEP_COUNT));
    EXPECT_THAT(memory.read_dword(TEST_MEMORY_SECOND_ADDRESS), Eq(static_cast<cpu::UDWORD32>(TEST_MEMORY_ADD_EXPECTED_VALUE)));
}
