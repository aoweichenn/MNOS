#include <array>
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
#include <mnos/cpu/instruction/condition_code.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/descriptor_tables.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr cpu::SignedQword TEST_PROGRAM_INITIAL_VALUE = cpu::SignedQword{1};
constexpr cpu::SignedQword TEST_LINEAR_ADD_VALUE = cpu::SignedQword{2};
constexpr cpu::SignedQword TEST_LINEAR_SUB_VALUE = cpu::SignedQword{1};
constexpr cpu::Qword TEST_LINEAR_EXPECTED_RAX = cpu::Qword{2};
constexpr cpu::SignedQword TEST_EXECUTOR_EXPECTED_VALUE = cpu::SignedQword{42};
constexpr cpu::SignedQword TEST_SKIPPED_BRANCH_VALUE = cpu::SignedQword{13};
constexpr cpu::SignedQword TEST_FALLTHROUGH_BRANCH_VALUE = cpu::SignedQword{7};
constexpr cpu::SignedQword TEST_SIGNED_QWORD_MAX = std::numeric_limits<cpu::SignedQword>::max();
constexpr cpu::SignedQword TEST_ADD_CARRY_LEFT_VALUE = cpu::SignedQword{-1};
constexpr cpu::SignedQword TEST_ADD_CARRY_RIGHT_VALUE = cpu::SignedQword{1};
constexpr cpu::SignedQword TEST_SUB_BORROW_LEFT_VALUE = cpu::SignedQword{0};
constexpr cpu::SignedQword TEST_SUB_BORROW_RIGHT_VALUE = cpu::SignedQword{1};
constexpr std::size_t TEST_PROGRAM_RESERVE_COUNT = 8;
constexpr std::size_t TEST_LINEAR_PROGRAM_STEP_COUNT = 4;
constexpr std::size_t TEST_BRANCH_PROGRAM_STEP_COUNT = 5;
constexpr cpu::SignedQword TEST_BRANCH_TARGET = cpu::SignedQword{4};
constexpr cpu::SignedQword TEST_JUMP_END_TARGET = cpu::SignedQword{5};
constexpr cpu::InstructionPointer TEST_FIRST_RIP = cpu::InstructionPointer{0};
constexpr cpu::InstructionPointer TEST_BRANCH_PROGRAM_FINAL_RIP = cpu::InstructionPointer{6};

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::Address64 TEST_MEMORY_BASE_ADDRESS = cpu::Address64{16};
constexpr cpu::Address64 TEST_MEMORY_SECOND_ADDRESS = cpu::Address64{24};
constexpr cpu::Address64 TEST_MEMORY_EFFECTIVE_ADDRESS = cpu::Address64{32};
constexpr cpu::SignedQword TEST_MEMORY_POSITIVE_DISPLACEMENT = cpu::SignedQword{16};
constexpr cpu::SignedQword TEST_MEMORY_NEGATIVE_BASE = cpu::SignedQword{32};
constexpr cpu::SignedQword TEST_MEMORY_NEGATIVE_DISPLACEMENT = cpu::SignedQword{-8};
constexpr cpu::SignedQword TEST_MEMORY_EXECUTOR_VALUE = cpu::SignedQword{0x123456789ABCDEF0LL};
constexpr cpu::SignedQword TEST_MEMORY_ADD_INITIAL_VALUE = cpu::SignedQword{10};
constexpr cpu::SignedQword TEST_MEMORY_ADD_INCREMENT = cpu::SignedQword{32};
constexpr cpu::SignedQword TEST_MEMORY_ADD_EXPECTED_VALUE = cpu::SignedQword{42};
constexpr std::size_t TEST_MEMORY_MOV_PROGRAM_STEP_COUNT = 5;
constexpr std::size_t TEST_MEMORY_ARITHMETIC_PROGRAM_STEP_COUNT = 6;
constexpr std::size_t TEST_MEMORY_NEGATIVE_PROGRAM_STEP_COUNT = 4;
constexpr cpu::SignedQword TEST_MEMORY_BRANCH_TARGET = cpu::SignedQword{5};
constexpr cpu::Address64 TEST_STAGE2_STACK_TOP = cpu::Address64{120};
constexpr cpu::SignedQword TEST_STAGE2_CALL_TARGET = cpu::SignedQword{7};
constexpr cpu::SignedQword TEST_STAGE2_RETURN_VALUE = cpu::SignedQword{13};
constexpr cpu::SignedQword TEST_STAGE2_INDEX_VALUE = cpu::SignedQword{3};
constexpr cpu::Qword TEST_STAGE2_LEA_EXPECTED_ADDRESS = cpu::Qword{44};
constexpr std::size_t TEST_STAGE2_STACK_PROGRAM_STEP_COUNT = 10;
constexpr cpu::Qword TEST_STAGE2_SETCC_PRESERVED_VALUE = cpu::Qword{0x123400};
constexpr cpu::Qword TEST_STAGE2_SETCC_EXPECTED_VALUE = cpu::Qword{0x123401};
constexpr cpu::Qword TEST_STAGE2_LOGIC_LEFT_VALUE = cpu::Qword{0xF0F0};
constexpr cpu::SignedQword TEST_STAGE2_LOGIC_RIGHT_VALUE = cpu::SignedQword{0x0F0F};
constexpr cpu::Byte TEST_STAGE2_ZERO_EXTEND_BYTE = cpu::Byte{0xF0};
constexpr cpu::Byte TEST_STAGE2_SIGN_EXTEND_BYTE = cpu::Byte{0x80};
constexpr cpu::Qword TEST_STAGE2_SIGN_EXTEND_EXPECTED = cpu::Qword{0xFFFF'FFFF'FFFF'FF80ULL};
constexpr cpu::Qword TEST_STAGE2_SIGN_EXTEND_REGISTER_EXPECTED = cpu::Qword{0xFFFF'FFFF'FFFF'FFF0ULL};
constexpr cpu::Qword TEST_STAGE2_PARTIAL_REGISTER_INITIAL = cpu::Qword{0x1234'5678'9ABC'DEF0ULL};
constexpr cpu::Qword TEST_STAGE2_PARTIAL_WORD_EXPECTED = cpu::Qword{0x1234'5678'9ABC'1111ULL};
constexpr cpu::Qword TEST_STAGE2_PARTIAL_DWORD_EXPECTED = cpu::Qword{0xFFFF'FFFFULL};
constexpr std::size_t TEST_STAGE2_LOGIC_PROGRAM_STEP_COUNT = 22;
constexpr cpu::SignedQword TEST_STAGE2_SIGNED_LEFT_VALUE = cpu::SignedQword{5};
constexpr cpu::SignedQword TEST_STAGE2_SIGNED_RIGHT_VALUE = cpu::SignedQword{10};
constexpr cpu::SignedQword TEST_STAGE2_JCC_TARGET = cpu::SignedQword{4};
constexpr std::size_t TEST_STAGE2_JCC_PROGRAM_STEP_COUNT = 5;
constexpr cpu::Qword TEST_STAGE3_USER_STACK_TOP = cpu::Qword{120};
constexpr cpu::Qword TEST_STAGE3_KERNEL_STACK_TOP = cpu::Qword{96};
constexpr cpu::SignedQword TEST_STAGE3_TRAP_HANDLER_RIP = cpu::SignedQword{4};
constexpr cpu::SignedQword TEST_STAGE3_SYSCALL_HANDLER_RIP = cpu::SignedQword{4};
constexpr cpu::SignedQword TEST_STAGE3_HANDLER_VALUE = cpu::SignedQword{13};
constexpr std::size_t TEST_STAGE3_TRAP_PROGRAM_STEP_COUNT = 6;
constexpr std::size_t TEST_STAGE3_SYSCALL_PROGRAM_STEP_COUNT = 6;
constexpr std::size_t TEST_STAGE4_MEMORY_SIZE_BYTES = 128 * 1024;
constexpr cpu::Address64 TEST_STAGE4_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 TEST_STAGE4_NEXT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 TEST_STAGE4_LINEAR_DATA_PAGE = cpu::Address64{0x5000};
constexpr cpu::Address64 TEST_STAGE4_PHYSICAL_DATA_PAGE = cpu::Address64{0x9000};
constexpr cpu::Address64 TEST_STAGE4_UNMAPPED_PAGE = cpu::Address64{0xA000};
constexpr cpu::SignedQword TEST_STAGE4_LINEAR_DATA_VALUE =
    static_cast<cpu::SignedQword>(TEST_STAGE4_LINEAR_DATA_PAGE);
constexpr cpu::SignedQword TEST_STAGE4_UNMAPPED_DATA_VALUE =
    static_cast<cpu::SignedQword>(TEST_STAGE4_UNMAPPED_PAGE);
constexpr cpu::SignedQword TEST_STAGE4_PAGE_FAULT_HANDLER_RIP = cpu::SignedQword{2};
constexpr cpu::Qword TEST_STAGE4_USER_STACK_TOP = cpu::Qword{0xF000};
constexpr cpu::Qword TEST_STAGE4_KERNEL_STACK_TOP = cpu::Qword{0xE000};
constexpr std::size_t TEST_STAGE4_PAGED_PROGRAM_STEP_COUNT = 5;
constexpr cpu::Qword TEST_STAGE4_USER_NOT_PRESENT_ERROR =
    cpu_memory::PAGE_FAULT_ERROR_USER_BIT;
constexpr cpu::Address64 TEST_STAGE6_ATOMIC_ADDRESS = cpu::Address64{48};
constexpr cpu::Qword TEST_STAGE6_COMPARE_VALUE = cpu::Qword{10};
constexpr cpu::Qword TEST_STAGE6_EXCHANGE_VALUE = cpu::Qword{42};
constexpr cpu::Qword TEST_STAGE6_ADD_VALUE = cpu::Qword{5};
constexpr cpu::Qword TEST_STAGE6_XADD_EXPECTED_VALUE = cpu::Qword{47};
constexpr cpu::Address64 TEST_STAGE7_INVLPG_LINEAR_PAGE = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_STAGE7_INVLPG_PHYSICAL_FRAME = cpu::Address64{0xA000};
constexpr cpu::Address64 TEST_STAGE7_INVLPG_LEAF_ENTRY = cpu::Address64{0xB000};
constexpr cpu_memory::ProcessContextId TEST_STAGE7_PCID{17};
constexpr cpu::SignedQword TEST_STAGE7_ZERO_DISPLACEMENT = cpu::SignedQword{0};

void map_stage4_instruction_page(cpu_memory::PageTableBuilder& builder)
{
    builder.map_4k(
        cpu::Address64{0},
        cpu::Address64{0},
        cpu_memory::PagePermissions::user_read_write_execute());
}
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
    EXPECT_THAT(executor.cycle_count(), Eq(cpu::Qword{0}));
    EXPECT_THAT(executor.step(state, program), Eq(cpu::StepResult::HALTED));
}

TEST(ExecutorProgramTest, RunsConditionalBranchProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_je_imm(TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    EXPECT_THAT(executed_steps, Eq(TEST_BRANCH_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.rip(), Eq(TEST_BRANCH_PROGRAM_FINAL_RIP));
}

TEST(ExecutorProgramTest, LetsJneFallThroughWhenZeroFlagIsSet)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_cmp_imm(cpu::RegisterId::RAX, TEST_PROGRAM_INITIAL_VALUE));
    program.push_back(cpu_support::make_jne_imm(TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_FALLTHROUGH_BRANCH_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_FALLTHROUGH_BRANCH_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
}

TEST(ExecutorProgramTest, RunsUnconditionalJumpProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_jmp_imm(TEST_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_jmp_imm(TEST_JUMP_END_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    static_cast<void>(executor.run(state, program));

    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
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
    EXPECT_THAT(carry_state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
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
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SignedQword>(TEST_MEMORY_BASE_ADDRESS)));
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
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS), Eq(static_cast<cpu::Qword>(TEST_MEMORY_EXECUTOR_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_MEMORY_EXECUTOR_VALUE)));
}

TEST(ExecutorProgramTest, RunsArithmeticAgainstMemoryOperands)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_MEMORY_EFFECTIVE_ADDRESS, static_cast<cpu::Qword>(TEST_MEMORY_ADD_INITIAL_VALUE));

    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SignedQword>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu::Instruction::make_add(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_INCREMENT)));
    program.push_back(cpu::Instruction::make_cmp(
        cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
        cpu::Operand::imm(TEST_MEMORY_ADD_EXPECTED_VALUE)));
    program.push_back(cpu_support::make_je_imm(TEST_MEMORY_BRANCH_TARGET));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_MEMORY_ARITHMETIC_PROGRAM_STEP_COUNT));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_EFFECTIVE_ADDRESS), Eq(static_cast<cpu::Qword>(TEST_MEMORY_ADD_EXPECTED_VALUE)));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
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
    EXPECT_THAT(memory.read_dword(TEST_MEMORY_SECOND_ADDRESS), Eq(static_cast<cpu::Dword>(TEST_MEMORY_ADD_EXPECTED_VALUE)));
}

TEST(ExecutorProgramTest, RunsStackCallRetAndLeaProgram)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT + std::size_t{2});
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RSP, static_cast<cpu::SignedQword>(TEST_STAGE2_STACK_TOP)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SignedQword>(TEST_MEMORY_BASE_ADDRESS)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RCX, TEST_STAGE2_INDEX_VALUE));
    program.push_back(cpu::Instruction::make_call(cpu::Operand::imm(TEST_STAGE2_CALL_TARGET)));
    program.push_back(cpu::Instruction::make_lea(
        cpu::Operand::reg(cpu::RegisterId::RDX),
        cpu::Operand::indexed_mem(
            cpu::RegisterId::RBP,
            cpu::RegisterId::RCX,
            cpu::MEMORY_ADDRESS_SCALE_4,
            TEST_MEMORY_POSITIVE_DISPLACEMENT,
            cpu::DataSize::QWORD)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());
    program.push_back(cpu::Instruction::make_push(cpu::Operand::imm(TEST_STAGE2_RETURN_VALUE)));
    program.push_back(cpu::Instruction::make_pop(cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_ret());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE2_STACK_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_STAGE2_RETURN_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RDX), Eq(TEST_STAGE2_LEA_EXPECTED_ADDRESS));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE2_STACK_TOP));
}

TEST(ExecutorProgramTest, RunsStage2LogicalConditionAndExtensionInstructions)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_byte(TEST_MEMORY_EFFECTIVE_ADDRESS, TEST_STAGE2_ZERO_EXTEND_BYTE);
    memory.write_byte(TEST_MEMORY_EFFECTIVE_ADDRESS + cpu::Address64{1}, TEST_STAGE2_SIGN_EXTEND_BYTE);

    cpu::Program program;
    program.reserve(TEST_STAGE2_LOGIC_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, static_cast<cpu::SignedQword>(TEST_MEMORY_EFFECTIVE_ADDRESS)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, static_cast<cpu::SignedQword>(TEST_STAGE2_SETCC_PRESERVED_VALUE)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, static_cast<cpu::SignedQword>(TEST_STAGE2_LOGIC_LEFT_VALUE)));
    program.push_back(cpu::Instruction::make_and(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(TEST_STAGE2_LOGIC_RIGHT_VALUE)));
    program.push_back(cpu::Instruction::make_setcc(cpu::ConditionCode::E, cpu::Operand::reg(cpu::RegisterId::RBX, cpu::DataSize::BYTE)));
    program.push_back(cpu::Instruction::make_cmovcc(
        cpu::ConditionCode::E,
        cpu::Operand::reg(cpu::RegisterId::RCX),
        cpu::Operand::reg(cpu::RegisterId::RBX)));
    program.push_back(cpu::Instruction::make_or(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(cpu::SignedQword{0xF0})));
    program.push_back(cpu::Instruction::make_xor(cpu::Operand::reg(cpu::RegisterId::RAX), cpu::Operand::imm(cpu::SignedQword{0xF0})));
    program.push_back(cpu::Instruction::make_movzx(
        cpu::Operand::reg(cpu::RegisterId::RDX),
        cpu_support::make_mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::BYTE)));
    program.push_back(cpu::Instruction::make_movsx(
        cpu::Operand::reg(cpu::RegisterId::RSI),
        cpu_support::make_mem(cpu::RegisterId::RBP, cpu::SignedQword{1}, cpu::DataSize::BYTE)));
    program.push_back(cpu::Instruction::make_movsx(
        cpu::Operand::reg(cpu::RegisterId::R11),
        cpu::Operand::reg(cpu::RegisterId::RDX, cpu::DataSize::BYTE)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::R8, cpu::SignedQword{0}));
    program.push_back(cpu_support::make_sub_imm(cpu::RegisterId::R8, cpu::SignedQword{1}));
    program.push_back(cpu::Instruction::make_setcc(cpu::ConditionCode::L, cpu::Operand::reg(cpu::RegisterId::R9, cpu::DataSize::BYTE)));
    program.push_back(cpu::Instruction::make_inc(cpu::Operand::reg(cpu::RegisterId::RDX)));
    program.push_back(cpu::Instruction::make_dec(cpu::Operand::reg(cpu::RegisterId::RDX)));
    program.push_back(cpu::Instruction::make_test(cpu::Operand::reg(cpu::RegisterId::RDX), cpu::Operand::imm(cpu::SignedQword{0xF0})));
    program.push_back(cpu::Instruction::make_setcc(cpu::ConditionCode::P, cpu::Operand::reg(cpu::RegisterId::R10, cpu::DataSize::BYTE)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::R14, static_cast<cpu::SignedQword>(TEST_STAGE2_PARTIAL_REGISTER_INITIAL)));
    program.push_back(cpu::Instruction::make_mov(cpu::Operand::reg(cpu::RegisterId::R14, cpu::DataSize::WORD), cpu::Operand::imm(cpu::SignedQword{0x1111})));
    program.push_back(cpu::Instruction::make_mov(cpu::Operand::reg(cpu::RegisterId::R13, cpu::DataSize::DWORD), cpu::Operand::imm(cpu::SignedQword{-1})));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE2_LOGIC_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{0}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(TEST_STAGE2_SETCC_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(TEST_STAGE2_SETCC_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RDX), Eq(static_cast<cpu::Qword>(TEST_STAGE2_ZERO_EXTEND_BYTE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSI), Eq(TEST_STAGE2_SIGN_EXTEND_EXPECTED));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R9), Eq(cpu::Qword{1}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R10), Eq(cpu::Qword{1}));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R11), Eq(TEST_STAGE2_SIGN_EXTEND_REGISTER_EXPECTED));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R14), Eq(TEST_STAGE2_PARTIAL_WORD_EXPECTED));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::R13), Eq(TEST_STAGE2_PARTIAL_DWORD_EXPECTED));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::CF));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::PF));
}

TEST(ExecutorProgramTest, EvaluatesAllStage2ConditionCodes)
{
    struct ConditionCase
    {
        cpu::ConditionCode condition;
        bool carry = false;
        bool parity = false;
        bool zero = false;
        bool sign = false;
        bool overflow = false;
    };

    constexpr std::array<ConditionCase, cpu::CONDITION_CODE_COUNT> CONDITION_CASES{
        ConditionCase{cpu::ConditionCode::O, false, false, false, false, true},
        ConditionCase{cpu::ConditionCode::NO},
        ConditionCase{cpu::ConditionCode::B, true},
        ConditionCase{cpu::ConditionCode::AE},
        ConditionCase{cpu::ConditionCode::E, false, false, true},
        ConditionCase{cpu::ConditionCode::NE},
        ConditionCase{cpu::ConditionCode::BE, true},
        ConditionCase{cpu::ConditionCode::A},
        ConditionCase{cpu::ConditionCode::S, false, false, false, true},
        ConditionCase{cpu::ConditionCode::NS},
        ConditionCase{cpu::ConditionCode::P, false, true},
        ConditionCase{cpu::ConditionCode::NP},
        ConditionCase{cpu::ConditionCode::L, false, false, false, true, false},
        ConditionCase{cpu::ConditionCode::GE},
        ConditionCase{cpu::ConditionCode::LE, false, false, true},
        ConditionCase{cpu::ConditionCode::G}};

    for (const ConditionCase test_case : CONDITION_CASES)
    {
        cpu::Program program{
            cpu::Instruction::make_setcc(
                test_case.condition,
                cpu::Operand::reg(cpu::RegisterId::RAX, cpu::DataSize::BYTE)),
        };
        cpu::CpuState state;
        state.flags().write(cpu::FlagId::CF, test_case.carry);
        state.flags().write(cpu::FlagId::PF, test_case.parity);
        state.flags().write(cpu::FlagId::ZF, test_case.zero);
        state.flags().write(cpu::FlagId::SF, test_case.sign);
        state.flags().write(cpu::FlagId::OF, test_case.overflow);
        cpu::Executor executor;

        EXPECT_THAT(executor.step(state, program), Eq(cpu::StepResult::EXECUTED));
        EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(cpu::Qword{1}));
    }
}

TEST(ExecutorProgramTest, RunsGenericSignedConditionalJump)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_STAGE2_SIGNED_LEFT_VALUE));
    program.push_back(cpu::Instruction::make_cmp(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::imm(TEST_STAGE2_SIGNED_RIGHT_VALUE)));
    program.push_back(cpu::Instruction::make_jcc(cpu::ConditionCode::L, cpu::Operand::imm(TEST_STAGE2_JCC_TARGET)));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_SKIPPED_BRANCH_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE2_JCC_PROGRAM_STEP_COUNT));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
}

TEST(ExecutorProgramTest, RunsStage3SoftwareInterruptAndIretProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RSP, static_cast<cpu::SignedQword>(TEST_STAGE3_USER_STACK_TOP)));
    program.push_back(cpu::Instruction::make_int(cpu_system::InterruptVector::breakpoint()));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_STAGE3_HANDLER_VALUE));
    program.push_back(cpu::Instruction::make_iret());

    cpu_system::TrapController trap_controller;
    trap_controller.idt().set_gate(
        cpu_system::InterruptVector::breakpoint(),
        cpu_system::InterruptGate::interrupt_gate(
            static_cast<cpu::Address64>(TEST_STAGE3_TRAP_HANDLER_RIP),
            cpu_system::PrivilegeLevel::RING0));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE3_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.flags().write(cpu::FlagId::IF, true);
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);
    cpu::ExecutionTrace trace;
    trace.reserve(TEST_STAGE3_TRAP_PROGRAM_STEP_COUNT);

    const std::size_t executed_steps = executor.run(state, program, cpu::EXECUTOR_DEFAULT_MAX_STEPS, &trace);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE3_TRAP_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_FALSE(state.has_pending_trap());
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::IF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE3_USER_STACK_TOP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_STAGE3_HANDLER_VALUE)));
    EXPECT_TRUE(trace.at(1).trap_pending_after);
    EXPECT_FALSE(trace.at(3).trap_pending_after);
}

TEST(ExecutorProgramTest, RunsStage3SyscallAndSysretProgram)
{
    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RSP, static_cast<cpu::SignedQword>(TEST_STAGE3_USER_STACK_TOP)));
    program.push_back(cpu::Instruction::make_syscall());
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_hlt());
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_STAGE3_HANDLER_VALUE));
    program.push_back(cpu::Instruction::make_sysret());

    cpu_system::TrapController trap_controller;
    trap_controller.configure_syscall(
        cpu_system::SyscallDescriptor::enabled(static_cast<cpu::Address64>(TEST_STAGE3_SYSCALL_HANDLER_RIP)));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE3_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.flags().write(cpu::FlagId::IF, true);
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);

    const std::size_t executed_steps = executor.run(state, program);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE3_SYSCALL_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_FALSE(state.has_pending_trap());
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING3));
    EXPECT_TRUE(state.flags().read(cpu::FlagId::IF));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE3_USER_STACK_TOP));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RAX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_STAGE3_HANDLER_VALUE)));
}

TEST(ExecutorProgramTest, RunsStage4PagedMemoryProgram)
{
    cpu::PhysicalMemory memory(TEST_STAGE4_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder page_table_builder{
        memory_bus,
        TEST_STAGE4_ROOT_TABLE,
        TEST_STAGE4_NEXT_TABLE};
    page_table_builder.clear_root_table();
    map_stage4_instruction_page(page_table_builder);
    page_table_builder.map_4k(
        TEST_STAGE4_LINEAR_DATA_PAGE,
        TEST_STAGE4_PHYSICAL_DATA_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());

    cpu::Program program;
    program.reserve(TEST_STAGE4_PAGED_PROGRAM_STEP_COUNT);
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBP, TEST_STAGE4_LINEAR_DATA_VALUE));
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RAX, TEST_EXECUTOR_EXPECTED_VALUE));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::QWORD),
        cpu::Operand::reg(cpu::RegisterId::RAX)));
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RBX),
        cpu::Operand::mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::QWORD)));
    program.push_back(cpu::Instruction::make_hlt());

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.paging().load_cr3(TEST_STAGE4_ROOT_TABLE);
    state.paging().enable();
    cpu::Executor executor;
    const std::size_t executed_steps = executor.run(state, program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(TEST_STAGE4_PAGED_PROGRAM_STEP_COUNT));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(memory.read_qword(TEST_STAGE4_PHYSICAL_DATA_PAGE), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(TEST_EXECUTOR_EXPECTED_VALUE)));
    EXPECT_FALSE(executor.mmu().tlb().empty());
}

TEST(ExecutorProgramTest, ExecutesStage6CmpxchgSuccessAndFailure)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_STAGE6_ATOMIC_ADDRESS, TEST_STAGE6_COMPARE_VALUE);

    cpu::Program success_program{
        cpu::Instruction::make_cmpxchg(
            cpu::Operand::absolute_mem(TEST_STAGE6_ATOMIC_ADDRESS, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RBX),
            true),
    };
    cpu::CpuState success_state;
    success_state.registers().write(cpu::RegisterId::RAX, TEST_STAGE6_COMPARE_VALUE);
    success_state.registers().write(cpu::RegisterId::RBX, TEST_STAGE6_EXCHANGE_VALUE);
    cpu::Executor executor;

    EXPECT_THAT(executor.step(success_state, success_program, memory_bus), Eq(cpu::StepResult::EXECUTED));
    EXPECT_THAT(memory.read_qword(TEST_STAGE6_ATOMIC_ADDRESS), Eq(TEST_STAGE6_EXCHANGE_VALUE));
    EXPECT_THAT(success_state.registers().read(cpu::RegisterId::RAX), Eq(TEST_STAGE6_COMPARE_VALUE));
    EXPECT_TRUE(success_state.flags().read(cpu::FlagId::ZF));
    EXPECT_FALSE(success_state.flags().read(cpu::FlagId::CF));

    cpu::Program failure_program{
        cpu::Instruction::make_cmpxchg(
            cpu::Operand::absolute_mem(TEST_STAGE6_ATOMIC_ADDRESS, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RBX),
            true),
    };
    cpu::CpuState failure_state;
    failure_state.registers().write(cpu::RegisterId::RAX, TEST_STAGE6_COMPARE_VALUE);
    failure_state.registers().write(cpu::RegisterId::RBX, cpu::Qword{99});
    executor.reset();

    EXPECT_THAT(executor.step(failure_state, failure_program, memory_bus), Eq(cpu::StepResult::EXECUTED));
    EXPECT_THAT(memory.read_qword(TEST_STAGE6_ATOMIC_ADDRESS), Eq(TEST_STAGE6_EXCHANGE_VALUE));
    EXPECT_THAT(failure_state.registers().read(cpu::RegisterId::RAX), Eq(TEST_STAGE6_EXCHANGE_VALUE));
    EXPECT_FALSE(failure_state.flags().read(cpu::FlagId::ZF));
    EXPECT_TRUE(failure_state.flags().read(cpu::FlagId::CF));
}

TEST(ExecutorProgramTest, ExecutesStage6XaddMfenceAndRejectsInvalidLock)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_STAGE6_ATOMIC_ADDRESS, TEST_STAGE6_EXCHANGE_VALUE);

    cpu::Program xadd_program{
        cpu::Instruction::make_xadd(
            cpu::Operand::absolute_mem(TEST_STAGE6_ATOMIC_ADDRESS, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RCX),
            true),
        cpu::Instruction::make_mfence(),
        cpu::Instruction::make_hlt(),
    };
    cpu::CpuState state;
    state.registers().write(cpu::RegisterId::RCX, TEST_STAGE6_ADD_VALUE);
    cpu::Executor executor;

    const std::size_t executed_steps = executor.run(state, xadd_program, memory_bus);

    EXPECT_THAT(executed_steps, Eq(std::size_t{3}));
    EXPECT_TRUE(state.is_halted());
    EXPECT_THAT(memory.read_qword(TEST_STAGE6_ATOMIC_ADDRESS), Eq(TEST_STAGE6_XADD_EXPECTED_VALUE));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RCX), Eq(TEST_STAGE6_EXCHANGE_VALUE));
    EXPECT_FALSE(state.flags().read(cpu::FlagId::ZF));

    cpu::Program invalid_lock_program{
        cpu::Instruction::make_xadd(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu::Operand::reg(cpu::RegisterId::RBX),
            true),
    };
    cpu::CpuState invalid_state;
    EXPECT_THROW(
        static_cast<void>(executor.step(invalid_state, invalid_lock_program, memory_bus)),
        std::logic_error);
}

TEST(ExecutorProgramTest, ExecutesStage7InvlpgForCurrentPcid)
{
    cpu::Program program{
        cpu::Instruction::make_invlpg(
            cpu::Operand::mem(cpu::RegisterId::RAX, TEST_STAGE7_ZERO_DISPLACEMENT, cpu::DataSize::BYTE)),
        cpu::Instruction::make_hlt(),
    };
    cpu::CpuState state;
    state.paging().set_process_context_id_enabled(true);
    state.paging().load_cr3(
        TEST_STAGE4_ROOT_TABLE,
        TEST_STAGE7_PCID,
        cpu_memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
    state.registers().write(cpu::RegisterId::RAX, TEST_STAGE7_INVLPG_LINEAR_PAGE);
    cpu::Executor executor;
    executor.mmu().tlb().insert(
        cpu_memory::PageTranslation{
            TEST_STAGE7_INVLPG_LINEAR_PAGE,
            TEST_STAGE7_INVLPG_PHYSICAL_FRAME,
            cpu_memory::PAGE_SIZE_4K_BYTES,
            cpu_memory::PagePermissions::kernel_read_write_execute(),
            TEST_STAGE7_INVLPG_LEAF_ENTRY,
            false},
        state.paging().generation(),
        TEST_STAGE7_PCID);

    EXPECT_THAT(executor.step(state, program), Eq(cpu::StepResult::EXECUTED));

    EXPECT_EQ(
        executor.mmu().tlb().lookup(TEST_STAGE7_INVLPG_LINEAR_PAGE, state.paging().generation(), TEST_STAGE7_PCID),
        nullptr);
    EXPECT_THAT(state.rip(), Eq(cpu::InstructionPointer{1}));
}

TEST(ExecutorProgramTest, RaisesGeneralProtectionForUserModeInvlpg)
{
    cpu::Program program{
        cpu::Instruction::make_invlpg(
            cpu::Operand::mem(cpu::RegisterId::RAX, TEST_STAGE7_ZERO_DISPLACEMENT, cpu::DataSize::BYTE)),
        cpu::Instruction::make_hlt(),
    };
    cpu_system::TrapController trap_controller;
    trap_controller.idt().set_gate(
        cpu_system::InterruptVector::general_protection(),
        cpu_system::InterruptGate::interrupt_gate(
            static_cast<cpu::Address64>(TEST_STAGE4_PAGE_FAULT_HANDLER_RIP),
            cpu_system::PrivilegeLevel::RING0));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE4_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.registers().write(cpu::RegisterId::RAX, TEST_STAGE7_INVLPG_LINEAR_PAGE);
    state.registers().write(cpu::RegisterId::RSP, TEST_STAGE4_USER_STACK_TOP);
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);

    EXPECT_THAT(executor.step(state, program), Eq(cpu::StepResult::EXECUTED));

    ASSERT_TRUE(state.has_pending_trap());
    EXPECT_THAT(state.pending_trap().vector(), Eq(cpu_system::InterruptVector::general_protection()));
    EXPECT_THAT(state.pending_trap().error_code(), Eq(cpu::Qword{0}));
    EXPECT_THAT(state.rip(), Eq(static_cast<cpu::InstructionPointer>(TEST_STAGE4_PAGE_FAULT_HANDLER_RIP)));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
}

TEST(ExecutorProgramTest, RaisesStage4PageFaultThroughTrapController)
{
    cpu::PhysicalMemory memory(TEST_STAGE4_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder page_table_builder{
        memory_bus,
        TEST_STAGE4_ROOT_TABLE,
        TEST_STAGE4_NEXT_TABLE};
    page_table_builder.clear_root_table();
    map_stage4_instruction_page(page_table_builder);

    cpu::Program program;
    program.reserve(TEST_PROGRAM_RESERVE_COUNT);
    program.push_back(cpu::Instruction::make_mov(
        cpu::Operand::reg(cpu::RegisterId::RAX),
        cpu::Operand::mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::QWORD)));
    program.push_back(cpu::Instruction::make_hlt());
    program.push_back(cpu_support::make_mov_imm(cpu::RegisterId::RBX, TEST_STAGE3_HANDLER_VALUE));
    program.push_back(cpu::Instruction::make_hlt());

    cpu_system::TrapController trap_controller;
    trap_controller.idt().set_gate(
        cpu_system::InterruptVector::page_fault(),
        cpu_system::InterruptGate::interrupt_gate(
            static_cast<cpu::Address64>(TEST_STAGE4_PAGE_FAULT_HANDLER_RIP),
            cpu_system::PrivilegeLevel::RING0));
    trap_controller.tss().set_privilege_stack(cpu_system::PrivilegeLevel::RING0, TEST_STAGE4_KERNEL_STACK_TOP);

    cpu::CpuState state;
    state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
    state.registers().write(cpu::RegisterId::RSP, TEST_STAGE4_USER_STACK_TOP);
    state.registers().write(cpu::RegisterId::RBP, static_cast<cpu::Qword>(TEST_STAGE4_UNMAPPED_DATA_VALUE));
    state.paging().load_cr3(TEST_STAGE4_ROOT_TABLE);
    state.paging().enable();
    cpu::Executor executor;
    executor.attach_trap_controller(trap_controller);

    EXPECT_THAT(executor.step(state, program, memory_bus), Eq(cpu::StepResult::EXECUTED));
    EXPECT_TRUE(state.has_pending_trap());
    EXPECT_THAT(state.pending_trap().vector(), Eq(cpu_system::InterruptVector::page_fault()));
    EXPECT_TRUE(state.pending_trap().has_error_code());
    EXPECT_THAT(state.pending_trap().error_code(), Eq(TEST_STAGE4_USER_NOT_PRESENT_ERROR));
    EXPECT_THAT(state.paging().page_fault_linear_address(), Eq(TEST_STAGE4_UNMAPPED_PAGE));
    EXPECT_THAT(state.rip(), Eq(static_cast<cpu::InstructionPointer>(TEST_STAGE4_PAGE_FAULT_HANDLER_RIP)));
    EXPECT_THAT(state.privilege_level(), Eq(cpu_system::PrivilegeLevel::RING0));
    EXPECT_THAT(state.registers().read(cpu::RegisterId::RSP), Eq(TEST_STAGE4_KERNEL_STACK_TOP));
}
