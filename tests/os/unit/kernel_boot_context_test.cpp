#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2});
constexpr std::size_t TEST_SMALL_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES / mm::AddressValue{2});
constexpr std::uint32_t TEST_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{4};
constexpr cpu::ADDRESS64 TEST_MEMORY_ADDRESS = cpu::ADDRESS64{16};
constexpr cpu::UQWORD64 TEST_MEMORY_VALUE = cpu::UQWORD64{0x12345678ULL};
}

TEST(MachineTest, OwnsPhysicalMemoryAndExposesMemoryBusFacade)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);

    EXPECT_THAT(machine.physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(machine.physical_memory().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(machine.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));

    machine.memory_bus().write(TEST_MEMORY_ADDRESS, cpu::DataSize::QWORD, TEST_MEMORY_VALUE);
    EXPECT_THAT(machine.physical_memory().read_qword(TEST_MEMORY_ADDRESS), Eq(TEST_MEMORY_VALUE));

    const platform::Machine& const_machine = machine;
    EXPECT_THAT(const_machine.physical_memory().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(const_machine.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));
}

TEST(BootContextTest, ExposesBootMachineResourcesAndProcessorCount)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext default_context{machine};
    kernel::BootContext explicit_context{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT};

    EXPECT_THAT(default_context.bootstrap_processor_count(), Eq(kernel::BOOT_CONTEXT_DEFAULT_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_THAT(explicit_context.bootstrap_processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_THAT(explicit_context.physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(explicit_context.physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(explicit_context.machine().physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));

    explicit_context.memory_bus().write(TEST_MEMORY_ADDRESS, cpu::DataSize::QWORD, TEST_MEMORY_VALUE);
    EXPECT_THAT(explicit_context.physical_memory().read_qword(TEST_MEMORY_ADDRESS), Eq(TEST_MEMORY_VALUE));

    const kernel::BootContext& const_context = explicit_context;
    EXPECT_THAT(const_context.machine().physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(const_context.physical_memory().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(const_context.memory_bus().read(TEST_MEMORY_ADDRESS, cpu::DataSize::QWORD), Eq(TEST_MEMORY_VALUE));
}

TEST(BootContextTest, RejectsMissingMemoryAndZeroProcessors)
{
    platform::Machine empty_machine(std::size_t{0});
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);

    EXPECT_THROW(static_cast<void>(kernel::BootContext{empty_machine}), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(kernel::BootContext{machine, std::uint32_t{0}}), std::invalid_argument);
}

TEST(KernelTest, BootsOnceWhenAtLeastOnePhysicalPageExists)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES);
    kernel::BootContext context{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{context};
    const kernel::Kernel& const_kernel = os_kernel;

    EXPECT_FALSE(os_kernel.is_booted());
    EXPECT_THAT(os_kernel.physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(os_kernel.physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(os_kernel.bootstrap_processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));

    os_kernel.boot();

    EXPECT_TRUE(os_kernel.is_booted());
    EXPECT_THAT(os_kernel.boot_context().physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(const_kernel.boot_context().physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THROW(os_kernel.boot(), std::logic_error);
}

TEST(KernelTest, RejectsBootWhenMemoryHasNoCompletePage)
{
    platform::Machine machine(TEST_SMALL_MEMORY_SIZE_BYTES);
    kernel::BootContext context{machine};
    kernel::Kernel os_kernel{context};

    EXPECT_THROW(os_kernel.boot(), std::runtime_error);
    EXPECT_FALSE(os_kernel.is_booted());
}
