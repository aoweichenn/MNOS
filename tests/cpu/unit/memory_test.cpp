#include <stdexcept>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>

namespace cpu = mnos::cpu;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_COUNT);

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr std::size_t TEST_MEMORY_RESIZED_SIZE_BYTES = 32;
constexpr std::size_t TEST_SINGLE_BYTE_COUNT = 1;
constexpr std::size_t TEST_TWO_BYTE_COUNT = 2;
constexpr cpu::Address64 TEST_MEMORY_BASE_ADDRESS = cpu::Address64{16};
constexpr cpu::Address64 TEST_MEMORY_SECOND_ADDRESS = cpu::Address64{24};
constexpr cpu::Qword TEST_MEMORY_BYTE_SOURCE_VALUE = cpu::Qword{0x1234};
constexpr cpu::Byte TEST_MEMORY_BYTE_EXPECTED_VALUE = cpu::Byte{0x34};
constexpr cpu::Word TEST_MEMORY_WORD_VALUE = cpu::Word{0xABCD};
constexpr cpu::Dword TEST_MEMORY_DWORD_VALUE = cpu::Dword{0xAABBCCDD};
constexpr cpu::Qword TEST_MEMORY_QWORD_VALUE = cpu::Qword{0x1122334455667788ULL};
constexpr cpu::Byte TEST_MEMORY_LOW_BYTE_VALUE = cpu::Byte{0x88};
constexpr cpu::Byte TEST_MEMORY_HIGH_BYTE_VALUE = cpu::Byte{0x11};
constexpr cpu::Byte TEST_MEMORY_FILL_VALUE = cpu::Byte{0xA5};
constexpr cpu::Byte TEST_MEMORY_INITIALIZER_FIRST_BYTE = cpu::Byte{0x12};
constexpr cpu::Byte TEST_MEMORY_INITIALIZER_SECOND_BYTE = cpu::Byte{0x34};
}

TEST(PhysicalMemoryTest, ContainerOperationsExposeOwnedBytes)
{
    cpu::PhysicalMemory memory;
    EXPECT_TRUE(memory.empty());

    memory.resize(TEST_MEMORY_RESIZED_SIZE_BYTES);
    EXPECT_FALSE(memory.empty());
    EXPECT_THAT(memory.size(), Eq(TEST_MEMORY_RESIZED_SIZE_BYTES));
    EXPECT_TRUE(memory.contains_range(TEST_MEMORY_BASE_ADDRESS, cpu::DATA_SIZE_QWORD_BYTES));
    EXPECT_FALSE(memory.contains_range(TEST_MEMORY_RESIZED_SIZE_BYTES, cpu::DATA_SIZE_BYTE_BYTES));
    EXPECT_FALSE(memory.contains_range(TEST_MEMORY_RESIZED_SIZE_BYTES + TEST_SINGLE_BYTE_COUNT, std::size_t{0}));

    const cpu::PhysicalMemory initialized_memory{
        TEST_MEMORY_INITIALIZER_FIRST_BYTE,
        TEST_MEMORY_INITIALIZER_SECOND_BYTE,
    };
    EXPECT_THAT(initialized_memory.size(), Eq(TEST_TWO_BYTE_COUNT));
    EXPECT_THAT(initialized_memory.read_byte(cpu::Address64{0}), Eq(TEST_MEMORY_INITIALIZER_FIRST_BYTE));

    std::vector<cpu::Byte> raw_bytes;
    raw_bytes.push_back(TEST_MEMORY_INITIALIZER_SECOND_BYTE);
    const cpu::PhysicalMemory vector_memory{std::move(raw_bytes)};
    EXPECT_THAT(vector_memory.size(), Eq(TEST_SINGLE_BYTE_COUNT));
    EXPECT_THAT(vector_memory.read_byte(cpu::Address64{0}), Eq(TEST_MEMORY_INITIALIZER_SECOND_BYTE));

    memory.clear();
    EXPECT_TRUE(memory.empty());
}

TEST(PhysicalMemoryTest, ReadsAndWritesLittleEndianValues)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_RESIZED_SIZE_BYTES);
    memory.write_qword(TEST_MEMORY_BASE_ADDRESS, TEST_MEMORY_QWORD_VALUE);

    EXPECT_THAT(memory.read_qword(TEST_MEMORY_BASE_ADDRESS), Eq(TEST_MEMORY_QWORD_VALUE));
    EXPECT_THAT(memory.bytes()[static_cast<std::size_t>(TEST_MEMORY_BASE_ADDRESS)], Eq(TEST_MEMORY_LOW_BYTE_VALUE));
    EXPECT_THAT(
        memory.bytes()[static_cast<std::size_t>(TEST_MEMORY_BASE_ADDRESS) + cpu::DATA_SIZE_QWORD_BYTES -
                       TEST_SINGLE_BYTE_COUNT],
        Eq(TEST_MEMORY_HIGH_BYTE_VALUE));

    memory.write_word(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_WORD_VALUE);
    EXPECT_THAT(memory.read_word(TEST_MEMORY_SECOND_ADDRESS), Eq(TEST_MEMORY_WORD_VALUE));

    memory.write_dword(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_DWORD_VALUE);
    EXPECT_THAT(memory.read_dword(TEST_MEMORY_SECOND_ADDRESS), Eq(TEST_MEMORY_DWORD_VALUE));

    memory.write(TEST_MEMORY_SECOND_ADDRESS, cpu::DataSize::BYTE, TEST_MEMORY_BYTE_SOURCE_VALUE);
    EXPECT_THAT(memory.read_byte(TEST_MEMORY_SECOND_ADDRESS), Eq(TEST_MEMORY_BYTE_EXPECTED_VALUE));

    memory.write_byte(TEST_MEMORY_SECOND_ADDRESS, TEST_MEMORY_FILL_VALUE);
    EXPECT_THAT(memory.read_byte(TEST_MEMORY_SECOND_ADDRESS), Eq(TEST_MEMORY_FILL_VALUE));

    const cpu::PhysicalMemory& const_memory = memory;
    EXPECT_THAT(const_memory.bytes().size(), Eq(memory.size()));

    memory.fill(TEST_MEMORY_FILL_VALUE);
    EXPECT_THAT(memory.read_byte(TEST_MEMORY_BASE_ADDRESS), Eq(TEST_MEMORY_FILL_VALUE));
}

TEST(PhysicalMemoryTest, RejectsOutOfRangeAndInvalidAccesses)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_RESIZED_SIZE_BYTES);

    EXPECT_THROW(static_cast<void>(memory.read_qword(TEST_MEMORY_RESIZED_SIZE_BYTES)), std::out_of_range);
    EXPECT_THROW(memory.write_word(TEST_MEMORY_RESIZED_SIZE_BYTES, TEST_MEMORY_WORD_VALUE), std::out_of_range);
    EXPECT_THROW(static_cast<void>(memory.read(TEST_MEMORY_BASE_ADDRESS, TEST_INVALID_DATA_SIZE)), std::out_of_range);
}

TEST(MemoryBusTest, DelegatesReadsWritesAndRangeQueriesToPhysicalMemory)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};

    EXPECT_FALSE(memory_bus.empty());
    EXPECT_THAT(memory_bus.size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_TRUE(memory_bus.contains_range(TEST_MEMORY_BASE_ADDRESS, cpu::DATA_SIZE_QWORD_BYTES));

    memory_bus.write(TEST_MEMORY_BASE_ADDRESS, cpu::DataSize::QWORD, TEST_MEMORY_QWORD_VALUE);
    EXPECT_THAT(memory_bus.read(TEST_MEMORY_BASE_ADDRESS, cpu::DataSize::QWORD), Eq(TEST_MEMORY_QWORD_VALUE));
    EXPECT_THAT(memory.read_qword(TEST_MEMORY_BASE_ADDRESS), Eq(TEST_MEMORY_QWORD_VALUE));
}
