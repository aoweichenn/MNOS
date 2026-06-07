#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace cpu = mnos::cpu;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{2});
constexpr std::size_t TEST_SMALL_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES / mm::AddressValue{2});
constexpr std::uint32_t TEST_BOOTSTRAP_PROCESSOR_COUNT = std::uint32_t{4};
constexpr cpu::Address64 TEST_MEMORY_ADDRESS = cpu::Address64{16};
constexpr cpu::Qword TEST_MEMORY_VALUE = cpu::Qword{0x12345678ULL};
constexpr mm::VirtualAddress TEST_KERNEL_STACK_BOTTOM{mm::AddressValue{0x20000}};
}

TEST(MachineTest, OwnsPhysicalMemoryAndExposesMemoryBusFacade)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_BOOTSTRAP_PROCESSOR_COUNT);

    EXPECT_THAT(machine.physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(machine.physical_memory().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(machine.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(machine.processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_THAT(machine.core_topology().core_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));

    machine.memory_bus().write(TEST_MEMORY_ADDRESS, cpu::DataSize::QWORD, TEST_MEMORY_VALUE);
    EXPECT_THAT(machine.physical_memory().read_qword(TEST_MEMORY_ADDRESS), Eq(TEST_MEMORY_VALUE));

    const platform::Machine& const_machine = machine;
    EXPECT_THAT(const_machine.physical_memory().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(const_machine.memory_bus().size(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(const_machine.core_topology().core_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
}

TEST(BootContextTest, ExposesBootMachineResourcesAndProcessorCount)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_BOOTSTRAP_PROCESSOR_COUNT);
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
    EXPECT_THROW(
        static_cast<void>(kernel::BootContext{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT}),
        std::invalid_argument);
}

TEST(KernelTest, BootsOnceWhenAtLeastOnePhysicalPageExists)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_BOOTSTRAP_PROCESSOR_COUNT);
    kernel::BootContext context{machine, TEST_BOOTSTRAP_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{context};
    const kernel::Kernel& const_kernel = os_kernel;

    EXPECT_FALSE(os_kernel.is_booted());
    EXPECT_FALSE(os_kernel.has_stage5_services());
    EXPECT_THAT(os_kernel.physical_memory_size_bytes(), Eq(TEST_MEMORY_SIZE_BYTES));
    EXPECT_THAT(os_kernel.physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(os_kernel.bootstrap_processor_count(), Eq(TEST_BOOTSTRAP_PROCESSOR_COUNT));
    EXPECT_THROW(static_cast<void>(os_kernel.physical_page_allocator()), std::logic_error);
    EXPECT_THROW(static_cast<void>(const_kernel.physical_page_allocator()), std::logic_error);
    EXPECT_THROW(static_cast<void>(os_kernel.kernel_address_space()), std::logic_error);
    EXPECT_THROW(static_cast<void>(const_kernel.kernel_address_space()), std::logic_error);
    EXPECT_THROW(static_cast<void>(os_kernel.process_at(std::size_t{0})), std::out_of_range);

    sched::ThreadContext thread{sched::ThreadId::first_kernel_thread(), TEST_KERNEL_STACK_BOTTOM};
    thread.cpu_state().registers().write(
        cpu::RegisterId::RAX,
        static_cast<cpu::Qword>(kernel::SyscallNumber::YIELD));
    EXPECT_THROW(static_cast<void>(os_kernel.dispatch_syscall(thread)), std::logic_error);

    os_kernel.boot();

    EXPECT_TRUE(os_kernel.is_booted());
    EXPECT_TRUE(os_kernel.has_stage5_services());
    EXPECT_THAT(os_kernel.boot_context().physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(const_kernel.boot_context().physical_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(const_kernel.physical_page_allocator().total_page_count(), Eq(std::uint64_t{2}));
    EXPECT_THAT(const_kernel.kernel_address_space().root_table_address(), Eq(mm::PhysicalAddress{mm::MM_PAGE_SIZE_BYTES}));
    EXPECT_TRUE(const_kernel.scheduler().empty());
    EXPECT_THROW(static_cast<void>(os_kernel.create_process()), std::bad_alloc);
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

TEST(KernelTest, OwnsProcessesAndRejectsOutOfRangeProcessAccess)
{
    platform::Machine machine(static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{32}));
    kernel::BootContext context{machine};
    kernel::Kernel os_kernel{context};
    os_kernel.boot();
    const kernel::Kernel& const_kernel = os_kernel;

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& thread = os_kernel.create_thread(process, TEST_KERNEL_STACK_BOTTOM);

    EXPECT_THAT(os_kernel.process_count(), Eq(std::size_t{1}));
    EXPECT_THAT(os_kernel.process_at(std::size_t{0}).id(), Eq(process.id()));
    EXPECT_THAT(const_kernel.process_at(std::size_t{0}).thread_at(std::size_t{0}).id(), Eq(thread.id()));
    EXPECT_THROW(static_cast<void>(os_kernel.process_at(std::size_t{1})), std::out_of_range);
    EXPECT_THROW(static_cast<void>(const_kernel.process_at(std::size_t{1})), std::out_of_range);
}
