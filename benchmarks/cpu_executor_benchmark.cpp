#include <cstddef>
#include <cstdint>

#include <benchmark/benchmark.h>

#include <cpu/support/cpu_test_helpers.hpp>
#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/execution/executor.hpp>
#include <mnos/cpu/execution/program.hpp>
#include <mnos/cpu/flags/id.hpp>
#include <mnos/cpu/instruction/instruction.hpp>
#include <mnos/cpu/instruction/operand.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/page_table_builder.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/cpu/system/trap_controller.hpp>

namespace cpu = mnos::cpu;
namespace cpu_support = mnos::test::cpu_support;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
constexpr std::size_t BENCHMARK_MEMORY_SIZE_BYTES = 4096;
constexpr cpu::SignedQword BENCHMARK_INITIAL_VALUE = cpu::SignedQword{1};
constexpr cpu::SignedQword BENCHMARK_INCREMENT_VALUE = cpu::SignedQword{41};
constexpr cpu::SignedQword BENCHMARK_MEMORY_BASE_VALUE = cpu::SignedQword{128};
constexpr cpu::SignedQword BENCHMARK_MEMORY_DISPLACEMENT = cpu::SignedQword{32};
constexpr std::size_t BENCHMARK_BYTE_IMAGE_INSTRUCTION_COUNT = 5;
constexpr std::size_t BENCHMARK_STAGE2_BYTE_IMAGE_INSTRUCTION_COUNT = 8;
constexpr std::size_t BENCHMARK_STAGE3_BYTE_IMAGE_INSTRUCTION_COUNT = 6;
constexpr std::size_t BENCHMARK_STAGE4_PAGED_PROGRAM_INSTRUCTION_COUNT = 5;
constexpr cpu::Address64 BENCHMARK_STAGE2_LOAD_ADDRESS = cpu::Address64{32};
constexpr cpu::Address64 BENCHMARK_STAGE3_SYSCALL_HANDLER_RIP = cpu::Address64{23};
constexpr cpu::Qword BENCHMARK_STAGE3_KERNEL_STACK_TOP = cpu::Qword{384};
constexpr std::size_t BENCHMARK_STAGE4_MEMORY_SIZE_BYTES = 128 * 1024;
constexpr cpu::Address64 BENCHMARK_STAGE4_ROOT_TABLE = cpu::Address64{0x1000};
constexpr cpu::Address64 BENCHMARK_STAGE4_NEXT_TABLE = cpu::Address64{0x2000};
constexpr cpu::Address64 BENCHMARK_STAGE4_LINEAR_DATA_PAGE = cpu::Address64{0x5000};
constexpr cpu::Address64 BENCHMARK_STAGE4_PHYSICAL_DATA_PAGE = cpu::Address64{0x9000};
constexpr cpu::SignedQword BENCHMARK_STAGE4_LINEAR_DATA_VALUE =
    static_cast<cpu::SignedQword>(BENCHMARK_STAGE4_LINEAR_DATA_PAGE);

[[nodiscard]] cpu::Program make_register_program()
{
    return cpu::Program{
        cpu_support::make_mov_imm(cpu::RegisterId::RAX, BENCHMARK_INITIAL_VALUE),
        cpu_support::make_add_imm(cpu::RegisterId::RAX, BENCHMARK_INCREMENT_VALUE),
        cpu_support::make_sub_imm(cpu::RegisterId::RAX, BENCHMARK_INITIAL_VALUE),
        cpu::Instruction::make_hlt(),
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
        cpu::Instruction::make_hlt(),
    };
}

[[nodiscard]] cpu::Program make_stage4_paged_program()
{
    return cpu::Program{
        cpu_support::make_mov_imm(cpu::RegisterId::RBP, BENCHMARK_STAGE4_LINEAR_DATA_VALUE),
        cpu_support::make_mov_imm(cpu::RegisterId::RAX, BENCHMARK_INCREMENT_VALUE),
        cpu::Instruction::make_mov(
            cpu::Operand::mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::QWORD),
            cpu::Operand::reg(cpu::RegisterId::RAX)),
        cpu::Instruction::make_mov(
            cpu::Operand::reg(cpu::RegisterId::RBX),
            cpu::Operand::mem(cpu::RegisterId::RBP, cpu::SignedQword{0}, cpu::DataSize::QWORD)),
        cpu::Instruction::make_hlt(),
    };
}

[[nodiscard]] cpu::ExecutableImage make_memory_image()
{
    return cpu::ExecutableImage{
        0x48, 0xBD, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 128
        0x48, 0xB8, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 41
        0x48, 0x89, 0x45, 0x20,                                     // MOV [RBP + 32], RAX
        0x48, 0x8B, 0x5D, 0x20,                                     // MOV RBX, [RBP + 32]
        0xF4};                                                      // HLT
}

[[nodiscard]] cpu::ExecutableImage make_stage2_image()
{
    return cpu::ExecutableImage{
        0x48, 0xBD, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBP, 32
        0x48, 0xB8, 0xF0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 0xF0F0
        0x48, 0x81, 0xE0, 0x0F, 0x0F, 0x00, 0x00,                   // AND RAX, 0x0F0F
        0x0F, 0x94, 0xC3,                                           // SETE BL
        0x48, 0x0F, 0x44, 0xCB,                                     // CMOVE RCX, RBX
        0x48, 0x0F, 0xB6, 0x55, 0x00,                               // MOVZX RDX, BYTE [RBP]
        0x48, 0x0F, 0xBE, 0x75, 0x01,                               // MOVSX RSI, BYTE [RBP + 1]
        0xF4};                                                      // HLT
}

[[nodiscard]] cpu::ExecutableImage make_stage3_image()
{
    return cpu::ExecutableImage{
        0x48, 0xBC, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RSP, 120
        0x0F, 0x05,                                                 // SYSCALL
        0x48, 0xB8, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RAX, 41
        0xF4,                                                       // HLT
        0x48, 0xBB, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MOV RBX, 13
        0x48, 0x0F, 0x07};                                          // SYSRETQ
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

static void BM_CPUExecutorByteImageMemoryProgram(benchmark::State& state)
{
    const cpu::ExecutableImage image = make_memory_image();

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu::CpuState cpu_state;
        cpu::PhysicalMemory memory(BENCHMARK_MEMORY_SIZE_BYTES);
        cpu::MemoryBus memory_bus{memory};
        cpu::Executor executor;
        benchmark::DoNotOptimize(executor.run(cpu_state, image, memory_bus));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RBX));
    }

    set_instruction_items_processed(state, BENCHMARK_BYTE_IMAGE_INSTRUCTION_COUNT);
}

static void BM_CPUExecutorStage2ByteImageProgram(benchmark::State& state)
{
    const cpu::ExecutableImage image = make_stage2_image();

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu::CpuState cpu_state;
        cpu::PhysicalMemory memory(BENCHMARK_MEMORY_SIZE_BYTES);
        memory.write_byte(BENCHMARK_STAGE2_LOAD_ADDRESS, cpu::Byte{0xF0});
        memory.write_byte(BENCHMARK_STAGE2_LOAD_ADDRESS + cpu::Address64{1}, cpu::Byte{0x80});
        cpu::MemoryBus memory_bus{memory};
        cpu::Executor executor;
        benchmark::DoNotOptimize(executor.run(cpu_state, image, memory_bus));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RSI));
    }

    set_instruction_items_processed(state, BENCHMARK_STAGE2_BYTE_IMAGE_INSTRUCTION_COUNT);
}

static void BM_CPUExecutorStage3SyscallByteImageProgram(benchmark::State& state)
{
    const cpu::ExecutableImage image = make_stage3_image();

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu_system::TrapController trap_controller;
        trap_controller.configure_syscall(cpu_system::SyscallDescriptor::enabled(BENCHMARK_STAGE3_SYSCALL_HANDLER_RIP));
        trap_controller.tss().set_privilege_stack(
            cpu_system::PrivilegeLevel::RING0,
            BENCHMARK_STAGE3_KERNEL_STACK_TOP);
        cpu::CpuState cpu_state;
        cpu_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
        cpu_state.flags().write(cpu::FlagId::IF, true);
        cpu::Executor executor;
        executor.attach_trap_controller(trap_controller);
        benchmark::DoNotOptimize(executor.run(cpu_state, image));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RAX));
    }

    set_instruction_items_processed(state, BENCHMARK_STAGE3_BYTE_IMAGE_INSTRUCTION_COUNT);
}

static void BM_CPUExecutorStage4PagedMemoryProgram(benchmark::State& state)
{
    const cpu::Program program = make_stage4_paged_program();
    cpu::PhysicalMemory memory(BENCHMARK_STAGE4_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::PageTableBuilder page_table_builder{
        memory_bus,
        BENCHMARK_STAGE4_ROOT_TABLE,
        BENCHMARK_STAGE4_NEXT_TABLE};
    page_table_builder.clear_root_table();
    page_table_builder.map_4k(
        cpu::Address64{0},
        cpu::Address64{0},
        cpu_memory::PagePermissions::user_read_write_execute());
    page_table_builder.map_4k(
        BENCHMARK_STAGE4_LINEAR_DATA_PAGE,
        BENCHMARK_STAGE4_PHYSICAL_DATA_PAGE,
        cpu_memory::PagePermissions::user_read_write_execute());

    for (auto unused_iteration : state)
    {
        static_cast<void>(unused_iteration);
        cpu::CpuState cpu_state;
        cpu_state.set_privilege_level(cpu_system::PrivilegeLevel::RING3);
        cpu_state.paging().load_cr3(BENCHMARK_STAGE4_ROOT_TABLE);
        cpu_state.paging().enable();
        cpu::Executor executor;
        benchmark::DoNotOptimize(executor.run(cpu_state, program, memory_bus));
        benchmark::DoNotOptimize(cpu_state.registers().read(cpu::RegisterId::RBX));
    }

    set_instruction_items_processed(state, BENCHMARK_STAGE4_PAGED_PROGRAM_INSTRUCTION_COUNT);
}

BENCHMARK(BM_CPUExecutorRegisterProgram);
BENCHMARK(BM_CPUExecutorMemoryProgram);
BENCHMARK(BM_CPUExecutorByteImageMemoryProgram);
BENCHMARK(BM_CPUExecutorStage2ByteImageProgram);
BENCHMARK(BM_CPUExecutorStage3SyscallByteImageProgram);
BENCHMARK(BM_CPUExecutorStage4PagedMemoryProgram);
