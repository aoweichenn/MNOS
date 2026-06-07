#include <array>
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
#include <support/deterministic_prng.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;
namespace cpu_support = mnos::test::cpu_support;

namespace
{
using ::testing::Eq;

constexpr std::uint64_t FUZZ_SEED = 0xF00D1234ULL;
constexpr std::size_t FUZZ_MEMORY_SIZE_BYTES = 256;
constexpr std::size_t FUZZ_MEMORY_CASE_COUNT = 192;
constexpr std::size_t FUZZ_EXECUTOR_CASE_COUNT = 80;
constexpr cpu::Address64 FUZZ_BASE_ADDRESS = cpu::Address64{32};
constexpr cpu::SignedQword FUZZ_BASE_REGISTER_VALUE = static_cast<cpu::SignedQword>(FUZZ_BASE_ADDRESS);
constexpr cpu::Qword FUZZ_BYTE_MASK = cpu::Qword{0xFF};

[[nodiscard]] cpu::DataSize fuzz_data_size(test::DeterministicPrng& prng) noexcept
{
    switch (prng.next_bounded(cpu::DATA_SIZE_COUNT))
    {
    case 0:
        return cpu::DataSize::BYTE;
    case 1:
        return cpu::DataSize::WORD;
    case 2:
        return cpu::DataSize::DWORD;
    default:
        return cpu::DataSize::QWORD;
    }
}

[[nodiscard]] cpu::Qword read_model(
    const std::array<cpu::Byte, FUZZ_MEMORY_SIZE_BYTES>& model,
    const cpu::Address64 address,
    const std::size_t byte_count) noexcept
{
    cpu::Qword value = cpu::Qword{0};
    const std::size_t start_index = static_cast<std::size_t>(address);
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * cpu::DATA_SIZE_BYTE_BITS;
        value |= static_cast<cpu::Qword>(model[start_index + byte_index]) << bit_shift;
    }
    return value;
}

void write_model(
    std::array<cpu::Byte, FUZZ_MEMORY_SIZE_BYTES>& model,
    const cpu::Address64 address,
    const std::size_t byte_count,
    const cpu::Qword value) noexcept
{
    const std::size_t start_index = static_cast<std::size_t>(address);
    for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
    {
        const std::size_t bit_shift = byte_index * cpu::DATA_SIZE_BYTE_BITS;
        model[start_index + byte_index] = static_cast<cpu::Byte>((value >> bit_shift) & FUZZ_BYTE_MASK);
    }
}

[[nodiscard]] cpu::Address64 fuzz_valid_address(test::DeterministicPrng& prng, const std::size_t byte_count) noexcept
{
    const std::size_t address_limit = FUZZ_MEMORY_SIZE_BYTES - byte_count;
    return static_cast<cpu::Address64>(prng.next_bounded(address_limit + std::size_t{1}));
}
}

TEST(MemoryExecutorFuzzTest, MemoryBusMatchesByteModel)
{
    test::DeterministicPrng prng{FUZZ_SEED};
    cpu::PhysicalMemory memory(FUZZ_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    std::array<cpu::Byte, FUZZ_MEMORY_SIZE_BYTES> model{};

    for (std::size_t case_index = 0; case_index < FUZZ_MEMORY_CASE_COUNT; ++case_index)
    {
        const cpu::DataSize data_size = fuzz_data_size(prng);
        const std::size_t byte_count = cpu::data_size_to_bytes(data_size);
        const cpu::Address64 address = fuzz_valid_address(prng, byte_count);
        const cpu::Qword value = prng.next();

        memory_bus.write(address, data_size, value);
        write_model(model, address, byte_count, value);

        EXPECT_THAT(memory_bus.read(address, data_size), Eq(read_model(model, address, byte_count)));
    }
}

TEST(MemoryExecutorFuzzTest, MemoryBusRejectsOutOfRangeReads)
{
    test::DeterministicPrng prng{FUZZ_SEED ^ std::uint64_t{0xBAD5EEDULL}};
    cpu::PhysicalMemory memory(FUZZ_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    for (std::size_t case_index = 0; case_index < FUZZ_EXECUTOR_CASE_COUNT; ++case_index)
    {
        const cpu::DataSize data_size = fuzz_data_size(prng);
        const cpu::Address64 address =
            static_cast<cpu::Address64>(FUZZ_MEMORY_SIZE_BYTES + prng.next_bounded(FUZZ_MEMORY_SIZE_BYTES));
        EXPECT_THROW(static_cast<void>(memory_bus.read(address, data_size)), std::out_of_range);
    }
}

TEST(MemoryExecutorFuzzTest, ExecutorRoundTripsValidMemoryShapes)
{
    test::DeterministicPrng prng{FUZZ_SEED ^ std::uint64_t{0x13579BDFULL}};

    for (std::size_t case_index = 0; case_index < FUZZ_EXECUTOR_CASE_COUNT; ++case_index)
    {
        const cpu::SignedQword displacement = static_cast<cpu::SignedQword>(
            prng.next_bounded(FUZZ_MEMORY_SIZE_BYTES - static_cast<std::size_t>(FUZZ_BASE_ADDRESS) -
                              cpu::DATA_SIZE_QWORD_BYTES));
        const cpu::SignedQword value = static_cast<cpu::SignedQword>(prng.next());

        cpu::Program program{
            cpu_support::make_mov_imm(cpu::RegisterId::RBP, FUZZ_BASE_REGISTER_VALUE),
            cpu_support::make_mov_imm(cpu::RegisterId::RAX, value),
            cpu::Instruction::make_mov(
                cpu_support::make_mem(cpu::RegisterId::RBP, displacement, cpu::DataSize::QWORD),
                cpu::Operand::reg(cpu::RegisterId::RAX)),
            cpu::Instruction::make_mov(
                cpu::Operand::reg(cpu::RegisterId::RBX),
                cpu_support::make_mem(cpu::RegisterId::RBP, displacement, cpu::DataSize::QWORD)),
            cpu::Instruction::make_hlt(),
        };

        cpu::PhysicalMemory memory(FUZZ_MEMORY_SIZE_BYTES);
        cpu::MemoryBus memory_bus{memory};
        cpu::CpuState state;
        cpu::Executor executor;
        static_cast<void>(executor.run(state, program, memory_bus));

        EXPECT_THAT(state.registers().read(cpu::RegisterId::RBX), Eq(static_cast<cpu::Qword>(value)));
    }
}

TEST(MemoryExecutorFuzzTest, ExecutorRejectsInvalidMemoryShapes)
{
    test::DeterministicPrng prng{FUZZ_SEED ^ std::uint64_t{0x2468ACE0ULL}};

    for (std::size_t case_index = 0; case_index < FUZZ_EXECUTOR_CASE_COUNT; ++case_index)
    {
        cpu::PhysicalMemory memory(FUZZ_MEMORY_SIZE_BYTES);
        cpu::MemoryBus memory_bus{memory};
        cpu::CpuState state;
        state.registers().write(cpu::RegisterId::RBP, FUZZ_BASE_ADDRESS);
        cpu::Executor executor;

        const cpu::SignedQword left_displacement =
            static_cast<cpu::SignedQword>(prng.next_bounded(FUZZ_MEMORY_SIZE_BYTES / cpu::DATA_SIZE_QWORD_BYTES));
        const cpu::SignedQword right_displacement =
            static_cast<cpu::SignedQword>(prng.next_bounded(FUZZ_MEMORY_SIZE_BYTES / cpu::DATA_SIZE_QWORD_BYTES));
        cpu::Program program{
            cpu::Instruction::make_mov(
                cpu_support::make_mem(cpu::RegisterId::RBP, left_displacement, cpu::DataSize::QWORD),
                cpu_support::make_mem(cpu::RegisterId::RBP, right_displacement, cpu::DataSize::QWORD)),
        };

        EXPECT_THROW(static_cast<void>(executor.step(state, program, memory_bus)), std::logic_error);
    }
}
