#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>

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

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
constexpr std::size_t BENCHMARK_MEMORY_SIZE_BYTES = 4096;
constexpr cpu::SQWORD64 BENCHMARK_INITIAL_VALUE = cpu::SQWORD64{1};
constexpr cpu::SQWORD64 BENCHMARK_INCREMENT_VALUE = cpu::SQWORD64{41};
constexpr cpu::SQWORD64 BENCHMARK_MEMORY_BASE_VALUE = cpu::SQWORD64{128};
constexpr cpu::SQWORD64 BENCHMARK_MEMORY_DISPLACEMENT = cpu::SQWORD64{32};

[[nodiscard]] cpu::Program make_register_program()
{
    return cpu::Program{
        cpu_support::make_mov_imm(cpu::RegisterId::RAX, BENCHMARK_INITIAL_VALUE),
        cpu_support::make_add_imm(cpu::RegisterId::RAX, BENCHMARK_INCREMENT_VALUE),
        cpu_support::make_sub_imm(cpu::RegisterId::RAX, BENCHMARK_INITIAL_VALUE),
        cpu::Instruction::make_halt(),
    };
}

[[nodiscard]] cpu::Program make_memory_program()
{
    return cpu::Program{
        cpu_support::make_mov_imm(cpu::RegisterId::RBP, BENCHMARK_MEMORY_BASE_VALUE),
        cpu_support::make_mov_imm(cpu::RegisterId::RAX, BENCHMARK_INCREMENT_VALUE),
        cpu::Instruction::make_mov(
            cpu_support::make_mem(cpu::RegisterId::RBP, BENCHMARK_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RAX)),
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RBX),
            cpu_support::make_mem(cpu::RegisterId::RBP, BENCHMARK_MEMORY_DISPLACEMENT, cpu::DataSize::QWORD)),
        cpu::Instruction::make_halt(),
    };
}

void set_instruction_items_processed(benchmark::State& state, const std::size_t program_size)
{
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(program_size));
}
}

static void BM_CPUExecutorRegisterProgram(benchmark::State& state)
{
    const cpu::Program program = make_register_program();

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu::CpuState cpu_state;
        cpu::Executor executor;
        benchmark::DoNotOptimize(executor.run(cpu_state, program));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RAX));
    }

    set_instruction_items_processed(state, program.size());
}

static void BM_CPUExecutorMemoryProgram(benchmark::State& state)
{
    const cpu::Program program = make_memory_program();

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu::CpuState cpu_state;
        cpu::PhysicalMemory memory(BENCHMARK_MEMORY_SIZE_BYTES);
        cpu::MemoryBus memory_bus{memory};
        cpu::Executor executor;
        benchmark::DoNotOptimize(executor.run(cpu_state, program, memory_bus));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RBX));
    }

    set_instruction_items_processed(state, program.size());
}

BENCHMARK(BM_CPUExecutorRegisterProgram);
BENCHMARK(BM_CPUExecutorMemoryProgram);
