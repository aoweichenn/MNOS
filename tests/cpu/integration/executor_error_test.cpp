#include <iostream>
#include <stdexcept>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
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
constexpr cpu::SQWORD64 TEST_IMMEDIATE_VALUE = cpu::SQWORD64{-42};
constexpr cpu::SQWORD64 TEST_MEMORY_DISPLACEMENT = cpu::SQWORD64{16};
constexpr cpu::SQWORD64 TEST_LOOP_TARGET = cpu::SQWORD64{0};
constexpr std::size_t TEST_LOOP_MAX_STEPS = 3;
constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr cpu::ADDRESS64 TEST_MEMORY_BASE_ADDRESS = cpu::ADDRESS64{16};
constexpr cpu::SQWORD64 TEST_MEMORY_POSITIVE_DISPLACEMENT = cpu::SQWORD64{16};
constexpr cpu::SQWORD64 TEST_PROGRAM_INITIAL_VALUE = cpu::SQWORD64{1};

void test_invalid_jump_target()
{
    cpu::Program invalid_jump_program{
        cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_MEMORY_DISPLACEMENT),
    };
    cpu::CpuState invalid_jump_state;
    cpu::Executor executor;

    test::check_throws<std::out_of_range>(
        [&executor, &invalid_jump_state, &invalid_jump_program]() {
            static_cast<void>(executor.step(invalid_jump_state, invalid_jump_program));
        },
        "invalid jump target");
}

void test_memory_operand_without_bus()
{
    cpu::Program memory_source_program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu::Operand::mem(cpu::RegisterId::RBP, TEST_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
    };
    cpu::CpuState memory_source_state;
    cpu::Executor executor;

    test::check_throws<std::logic_error>(
        [&executor, &memory_source_state, &memory_source_program]() {
            static_cast<void>(executor.step(memory_source_state, memory_source_program));
        },
        "memory operand without memory bus");
}

void test_memory_to_memory_instruction()
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

    test::check_throws<std::logic_error>(
        [&executor, &memory_to_memory_state, &memory_to_memory_program, &memory_bus]() {
            static_cast<void>(executor.step(memory_to_memory_state, memory_to_memory_program, memory_bus));
        },
        "memory-to-memory instruction");
}

void test_out_of_range_memory_execution()
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu::Program out_of_range_memory_program{
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RAX),
            cpu_support::make_mem(
                cpu::RegisterId::RBP,
                static_cast<cpu::SQWORD64>(TEST_MEMORY_SIZE_BYTES),
                cpu::DataSize::QWORD)),
    };
    cpu::CpuState out_of_range_memory_state;
    cpu::Executor executor;

    test::check_throws<std::out_of_range>(
        [&executor, &out_of_range_memory_state, &out_of_range_memory_program, &memory_bus]() {
            static_cast<void>(executor.step(out_of_range_memory_state, out_of_range_memory_program, memory_bus));
        },
        "out-of-range memory execution");
}

void test_invalid_destination_and_none_operand()
{
    cpu::Executor executor;

    cpu::Program non_register_destination_program{
        cpu::Instruction::make_mov(cpu::Operand::imm(TEST_IMMEDIATE_VALUE), cpu::Operand::imm(TEST_IMMEDIATE_VALUE)),
    };
    cpu::CpuState non_register_destination_state;
    test::check_throws<std::logic_error>(
        [&executor, &non_register_destination_state, &non_register_destination_program]() {
            static_cast<void>(executor.step(non_register_destination_state, non_register_destination_program));
        },
        "non-register destination");

    cpu::Program none_operand_program{
        cpu::Instruction::make_add(cpu::Operand::none(), cpu::Operand::imm(TEST_PROGRAM_INITIAL_VALUE)),
    };
    cpu::CpuState none_operand_state;
    executor.reset();
    test::check_throws<std::logic_error>(
        [&executor, &none_operand_state, &none_operand_program]() {
            static_cast<void>(executor.step(none_operand_state, none_operand_program));
        },
        "none operand read");
}

void test_executor_max_steps()
{
    cpu::Program loop_program{
        cpu_support::make_jump_imm(cpu::Opcode::JMP, TEST_LOOP_TARGET),
    };
    cpu::CpuState loop_state;
    cpu::Executor executor;
    test::check_throws<std::runtime_error>(
        [&executor, &loop_state, &loop_program]() {
            static_cast<void>(executor.run(loop_state, loop_program, TEST_LOOP_MAX_STEPS));
        },
        "executor max steps");
}
}

int main()
{
    test_invalid_jump_target();
    test_memory_operand_without_bus();
    test_memory_to_memory_instruction();
    test_out_of_range_memory_execution();
    test_invalid_destination_and_none_operand();
    test_executor_max_steps();

    std::cout << "mnos_cpu_executor_error_integration_tests passed\n";
    return 0;
}
