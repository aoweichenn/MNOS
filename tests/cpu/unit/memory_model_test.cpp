#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/memory_model.hpp>
#include <mnos/cpu/memory/physical_memory.hpp>
#include <mnos/cpu/system/core_id.hpp>
#include <mnos/cpu/system/core_topology.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_COUNT);
constexpr std::size_t TEST_MEMORY_SIZE_BYTES = 128;
constexpr std::uint32_t TEST_CORE_COUNT = std::uint32_t{2};
constexpr cpu_system::CoreId TEST_CORE0 = cpu_system::CoreId{0};
constexpr cpu_system::CoreId TEST_CORE1 = cpu_system::CoreId{1};
constexpr cpu_system::CoreId TEST_UNKNOWN_CORE = cpu_system::CoreId{7};
constexpr cpu::Address64 TEST_ADDRESS = cpu::Address64{16};
constexpr cpu::Address64 TEST_SECOND_ADDRESS = cpu::Address64{32};
constexpr cpu::Address64 TEST_BYTE_PATCH_ADDRESS = TEST_ADDRESS + cpu::Address64{2};
constexpr cpu::Qword TEST_INITIAL_MEMORY_VALUE = cpu::Qword{0x1122'3344'5566'7788ULL};
constexpr cpu::Qword TEST_BUFFERED_STORE_VALUE = cpu::Qword{0xAABB'CCDD'EEFF'0011ULL};
constexpr cpu::Qword TEST_OVERLAY_EXPECTED_VALUE = cpu::Qword{0x1122'3344'55AA'7788ULL};
constexpr cpu::Byte TEST_PATCH_BYTE_VALUE = cpu::Byte{0xAA};
constexpr cpu::Qword TEST_ATOMIC_EXPECTED_VALUE = cpu::Qword{10};
constexpr cpu::Qword TEST_ATOMIC_DESIRED_VALUE = cpu::Qword{20};
constexpr cpu::Qword TEST_ATOMIC_ADDEND = cpu::Qword{5};
}

TEST(X86TsoMemoryModelTest, BuffersStoresAndForwardsOnlyToOwningCore)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::X86TsoMemoryModel memory_model{cpu_system::CoreTopology{TEST_CORE_COUNT}};

    memory.write_qword(TEST_ADDRESS, TEST_INITIAL_MEMORY_VALUE);
    memory_model.store(TEST_CORE0, TEST_ADDRESS, cpu::DataSize::QWORD, TEST_BUFFERED_STORE_VALUE);

    EXPECT_THAT(memory_model.topology().core_count(), Eq(TEST_CORE_COUNT));
    EXPECT_THAT(memory_model.pending_store_count(), Eq(std::size_t{1}));
    EXPECT_THAT(memory_model.pending_store_count(TEST_CORE0), Eq(std::size_t{1}));
    EXPECT_TRUE(memory_model.has_pending_stores(TEST_CORE0));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(TEST_INITIAL_MEMORY_VALUE));
    EXPECT_THAT(
        memory_model.load(TEST_CORE0, memory_bus, TEST_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_BUFFERED_STORE_VALUE));
    EXPECT_THAT(
        memory_model.load(TEST_CORE1, memory_bus, TEST_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_INITIAL_MEMORY_VALUE));
}

TEST(X86TsoMemoryModelTest, ComposesForwardedBytesOverGloballyVisibleMemory)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::X86TsoMemoryModel memory_model{cpu_system::CoreTopology{TEST_CORE_COUNT}};

    memory.write_qword(TEST_ADDRESS, TEST_INITIAL_MEMORY_VALUE);
    memory_model.store(TEST_CORE0, TEST_BYTE_PATCH_ADDRESS, cpu::DataSize::BYTE, TEST_PATCH_BYTE_VALUE);

    EXPECT_THAT(
        memory_model.load(TEST_CORE0, memory_bus, TEST_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_OVERLAY_EXPECTED_VALUE));
    EXPECT_THAT(
        memory_model.load(TEST_CORE1, memory_bus, TEST_ADDRESS, cpu::DataSize::QWORD),
        Eq(TEST_INITIAL_MEMORY_VALUE));
}

TEST(X86TsoMemoryModelTest, DrainsStoresDeterministicallyAndSupportsFence)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::X86TsoMemoryModel memory_model{cpu_system::CoreTopology{TEST_CORE_COUNT}};

    memory_model.store(TEST_CORE0, TEST_ADDRESS, cpu::DataSize::QWORD, cpu::Qword{1});
    memory_model.store(TEST_CORE1, TEST_SECOND_ADDRESS, cpu::DataSize::QWORD, cpu::Qword{2});

    EXPECT_TRUE(memory_model.drain_one(memory_bus));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(cpu::Qword{1}));
    EXPECT_THAT(memory.read_qword(TEST_SECOND_ADDRESS), Eq(cpu::Qword{0}));

    memory_model.fence(memory_bus, TEST_CORE1);
    EXPECT_FALSE(memory_model.has_pending_stores(TEST_CORE1));
    EXPECT_THAT(memory.read_qword(TEST_SECOND_ADDRESS), Eq(cpu::Qword{2}));
    EXPECT_FALSE(memory_model.drain_core(memory_bus, TEST_CORE1));

    memory_model.store(TEST_CORE0, TEST_ADDRESS, cpu::DataSize::QWORD, cpu::Qword{3});
    memory_model.store(TEST_CORE1, TEST_SECOND_ADDRESS, cpu::DataSize::QWORD, cpu::Qword{4});
    memory_model.drain_all(memory_bus);
    EXPECT_THAT(memory_model.pending_store_count(), Eq(std::size_t{0}));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(cpu::Qword{3}));
    EXPECT_THAT(memory.read_qword(TEST_SECOND_ADDRESS), Eq(cpu::Qword{4}));
    EXPECT_FALSE(memory_model.drain_one(memory_bus));
}

TEST(X86TsoMemoryModelTest, AtomicOperationsDrainOwnerBufferAndWriteThrough)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::X86TsoMemoryModel memory_model{cpu_system::CoreTopology{TEST_CORE_COUNT}};

    memory_model.store(TEST_CORE0, TEST_ADDRESS, cpu::DataSize::QWORD, TEST_ATOMIC_EXPECTED_VALUE);
    const cpu_memory::AtomicCompareExchangeResult exchanged = memory_model.compare_exchange(
        memory_bus,
        TEST_CORE0,
        TEST_ADDRESS,
        cpu::DataSize::QWORD,
        TEST_ATOMIC_EXPECTED_VALUE,
        TEST_ATOMIC_DESIRED_VALUE);

    EXPECT_TRUE(exchanged.exchanged);
    EXPECT_THAT(exchanged.observed, Eq(TEST_ATOMIC_EXPECTED_VALUE));
    EXPECT_FALSE(memory_model.has_pending_stores(TEST_CORE0));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(TEST_ATOMIC_DESIRED_VALUE));

    const cpu_memory::AtomicCompareExchangeResult failed_exchange = memory_model.compare_exchange(
        memory_bus,
        TEST_CORE1,
        TEST_ADDRESS,
        cpu::DataSize::QWORD,
        TEST_ATOMIC_EXPECTED_VALUE,
        cpu::Qword{99});
    EXPECT_FALSE(failed_exchange.exchanged);
    EXPECT_THAT(failed_exchange.observed, Eq(TEST_ATOMIC_DESIRED_VALUE));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(TEST_ATOMIC_DESIRED_VALUE));

    const cpu_memory::AtomicFetchAddResult fetch_add = memory_model.fetch_add(
        memory_bus,
        TEST_CORE1,
        TEST_ADDRESS,
        cpu::DataSize::QWORD,
        TEST_ATOMIC_ADDEND);
    EXPECT_THAT(fetch_add.old_value, Eq(TEST_ATOMIC_DESIRED_VALUE));
    EXPECT_THAT(fetch_add.new_value, Eq(TEST_ATOMIC_DESIRED_VALUE + TEST_ATOMIC_ADDEND));
    EXPECT_THAT(memory.read_qword(TEST_ADDRESS), Eq(TEST_ATOMIC_DESIRED_VALUE + TEST_ATOMIC_ADDEND));
}

TEST(X86TsoMemoryModelTest, RejectsUnknownCoreAndInvalidDataSize)
{
    cpu::PhysicalMemory memory(TEST_MEMORY_SIZE_BYTES);
    cpu::MemoryBus memory_bus{memory};
    cpu_memory::X86TsoMemoryModel memory_model{cpu_system::CoreTopology{TEST_CORE_COUNT}};

    EXPECT_THROW(
        static_cast<void>(memory_model.pending_store_count(TEST_UNKNOWN_CORE)),
        std::out_of_range);
    EXPECT_THROW(
        memory_model.store(TEST_CORE0, TEST_ADDRESS, TEST_INVALID_DATA_SIZE, cpu::Qword{1}),
        std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(memory_model.load(TEST_CORE0, memory_bus, TEST_ADDRESS, TEST_INVALID_DATA_SIZE)),
        std::out_of_range);
}
