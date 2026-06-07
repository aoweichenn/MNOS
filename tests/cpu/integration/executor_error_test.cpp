#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cpu/support/cpu_test_helpers.hpp>
#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;
namespace cpu_system = mnos::cpu::system;

namespace
{
constexpr cpu::SignedQword TEST_IMMEDIATE_VALUE = cpu::SignedQword{-42};
constexpr cpu::SignedQword TEST_MEMORY_DISPLACEMENT = cpu::SignedQword{16};
constexpr cpu::SignedQword TEST_LOOP_TARGET = cpu::SignedQword{0};
constexpr std::size_t TEST_LOOP_MAX_STEPS = 3;
constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::Address64 TEST_MEMORY_BASE_ADDRESS = cpu::Address64{16};
constexpr cpu::Address64 TEST_STACK_TOP_ADDRESS = cpu::Address64{64};
constexpr cpu::SignedQword TEST_MEMORY_POSITIVE_DISPLACEMENT = cpu::SignedQword{16};
constexpr cpu::SignedQword TEST_PROGRAM_INITIAL_VALUE = cpu::SignedQword{1};
constexpr cpu::Qword TEST_INVALID_RETURN_TARGET = cpu::Qword{4};
}

TEST(ExecutorErrorTest, RejectsInvalidJumpTarget)
{
    cpu::Program invalid_jump_program{
        cpu_support::make_jmp_imm(TEST_MEMORY_DISPLACEMENT),
    };
    cpu::CpuState invalid_jump_state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(invalid_jump_state, invalid_jump_program)), std::out_of_range);
}

TEST(ExecutorErrorTest, RejectsMemoryOperandWithoutBus)
{
    cpu::Program memory_source_program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
    };
    cpu::CpuState memory_source_state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(memory_source_state, memory_source_program)), std::logic_error);
}

TEST(ExecutorErrorTest, RejectsStackInstructionsWithoutBus)
{
    cpu::Program push_program{
        cpu::Instruction::make_push(cpu::Operand::imm(TEST_PROGRAM_INITIAL_VALUE)),
    };
    cpu::CpuState push_state;
    push_state.registers().write(cpu::RegisterId::RSP, TEST_STACK_TOP_ADDRESS);
    cpu::Executor executor;
    EXPECT_THROW(static_cast<void>(executor.step(push_state, push_program)), std::logic_error);

    cpu::Program ret_program{
        cpu::Instruction::make_ret(),
    };
    cpu::CpuState ret_state;
    ret_state.registers().write(cpu::RegisterId::RSP, TEST_STACK_TOP_ADDRESS);
    executor.reset();
    EXPECT_THROW(static_cast<void>(executor.step(ret_state, ret_program)), std::logic_error);
}

TEST(ExecutorErrorTest, RejectsInvalidRetTarget)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    memory.write_qword(TEST_STACK_TOP_ADDRESS, TEST_INVALID_RETURN_TARGET);
    cpu::Program ret_program{
        cpu::Instruction::make_ret(),
        cpu::Instruction::make_hlt(),
    };
    cpu::CpuState ret_state;
    ret_state.registers().write(cpu::RegisterId::RSP, TEST_STACK_TOP_ADDRESS);
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(ret_state, ret_program, memory_bus)), std::out_of_range);
}

TEST(ExecutorErrorTest, RejectsMemoryToMemoryInstruction)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::Program memory_to_memory_program{
        cpu::Instruction::make_mov(
            cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_POSITIVE_DISPLACEMENT, cpu::DataSize::QWORD),
            cpu_support::make_mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
    };
    cpu::CpuState memory_to_memory_state;
    memory_to_memory_state.registers().write(cpu::RegisterId::RBP, TEST_MEMORY_BASE_ADDRESS);
    cpu::Executor executor;

    EXPECT_THROW(
        static_cast<void>(executor.step(memory_to_memory_state, memory_to_memory_program, memory_bus)),
        std::logic_error);
}

TEST(ExecutorErrorTest, RejectsOutOfRangeMemoryExecution)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::Program out_of_range_memory_program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu_support::make_mem(
                cpu::RegisterId::RBP,
                static_cast<cpu::SignedQword>(TEST_MEMORY_SIZE_BYTES),
                cpu::DataSize::QWORD)),
    };
    cpu::CpuState out_of_range_memory_state;
    cpu::Executor executor;

    EXPECT_THROW(
        static_cast<void>(executor.step(out_of_range_memory_state, out_of_range_memory_program, memory_bus)),
        std::out_of_range);
}

TEST(ExecutorErrorTest, RejectsInvalidDestinationAndNoneOperandReads)
{
    cpu::Executor executor;

    cpu::Program non_register_destination_program{
        cpu::Instruction::make_mov(cpu::Operand::imm(TEST_IMMEDIATE_VALUE), cpu::Operand::imm(TEST_IMMEDIATE_VALUE)),
    };
    cpu::CpuState non_register_destination_state;
    EXPECT_THROW(
        static_cast<void>(executor.step(non_register_destination_state, non_register_destination_program)),
        std::logic_error);

    cpu::Program none_operand_program{
        cpu::Instruction::make_add(cpu::Operand::none(), cpu::Operand::imm(TEST_PROGRAM_INITIAL_VALUE)),
    };
    cpu::CpuState none_operand_state;
    executor.reset();
    EXPECT_THROW(static_cast<void>(executor.step(none_operand_state, none_operand_program)), std::logic_error);
}

TEST(ExecutorErrorTest, EnforcesMaxStepLimit)
{
    cpu::Program loop_program{
        cpu_support::make_jmp_imm(TEST_LOOP_TARGET),
    };
    cpu::CpuState loop_state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.run(loop_state, loop_program, TEST_LOOP_MAX_STEPS)), std::runtime_error);
}

TEST(ExecutorErrorTest, RejectsTrapInstructionsWithoutControllerOrSyscallDescriptor)
{
    cpu::Program int_program{
        cpu::Instruction::make_int(cpu_system::InterruptVector::breakpoint()),
    };
    cpu::CpuState int_state;
    cpu::Executor executor;

    EXPECT_THROW(static_cast<void>(executor.step(int_state, int_program)), std::logic_error);

    cpu_system::TrapController trap_controller;
    executor.attach_trap_controller(trap_controller);
    cpu::Program syscall_program{
        cpu::Instruction::make_syscall(),
    };
    cpu::CpuState syscall_state;

    EXPECT_THROW(static_cast<void>(executor.step(syscall_state, syscall_program)), std::logic_error);
}
