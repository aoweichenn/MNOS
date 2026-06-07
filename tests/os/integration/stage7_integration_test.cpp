#include <cstddef>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/memory/tlb_shootdown.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>

namespace cpu = mnos::cpu;
namespace cpu_memory = mnos::cpu::memory;
namespace cpu_system = mnos::cpu::system;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{96});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr cpu_system::CoreId TEST_BOOT_CORE{0};
constexpr cpu_system::CoreId TEST_SECOND_CORE{1};
constexpr cpu::Address64 TEST_LINEAR_PAGE = cpu::Address64{0x4000};
constexpr cpu::Address64 TEST_PHYSICAL_FRAME = cpu::Address64{0x24000};
constexpr cpu::Address64 TEST_LEAF_ENTRY = cpu::Address64{0x34000};
constexpr cpu::Qword TEST_TLB_GENERATION = cpu::Qword{9};
constexpr cpu_memory::ProcessContextId TEST_PCID{11};

[[nodiscard]] cpu_memory::PageTranslation make_translation()
{
    return cpu_memory::PageTranslation{
        TEST_LINEAR_PAGE,
        TEST_PHYSICAL_FRAME,
        cpu_memory::PAGE_SIZE_4K_BYTES,
        cpu_memory::PagePermissions::kernel_read_write_execute(),
        TEST_LEAF_ENTRY,
        false};
}
}

TEST(Stage7IntegrationTest, TimerSleepAndShootdownFlowAcrossKernelAndCpu)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage7_services());

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& first_thread = os_kernel.create_thread(process);
    sched::ThreadContext& second_thread = os_kernel.create_thread(process);
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    ASSERT_NE(os_kernel.sleep_current_for(sched::SchedulerTick{1}), nullptr);
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(second_thread.id()));

    const std::optional<cpu_system::ApicInterrupt> timer_interrupt = os_kernel.tick_core_timer(TEST_BOOT_CORE);
    ASSERT_TRUE(timer_interrupt.has_value());
    EXPECT_THAT(timer_interrupt->kind(), Eq(cpu_system::ApicInterruptKind::TIMER));
    EXPECT_THAT(os_kernel.scheduler_tick_count(), Eq(sched::SchedulerTick{1}));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(first_thread.id()));

    cpu_memory::MemoryManagementUnit target_mmu;
    target_mmu.tlb().insert(make_translation(), TEST_TLB_GENERATION, TEST_PCID);
    const cpu_memory::TlbShootdownRequest& request = os_kernel.request_tlb_shootdown_page(
        TEST_BOOT_CORE,
        TEST_SECOND_CORE,
        TEST_LINEAR_PAGE,
        TEST_PCID);
    const cpu::Qword sequence = request.sequence();

    const std::optional<cpu_system::ApicInterrupt> shootdown_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_SECOND_CORE);
    ASSERT_TRUE(shootdown_ipi.has_value());
    EXPECT_THAT(shootdown_ipi->vector(), Eq(cpu_system::InterruptVector::tlb_shootdown()));

    const std::optional<cpu_memory::TlbShootdownRequest> pending_request =
        os_kernel.tlb_shootdown_controller().take_next_for(TEST_SECOND_CORE);
    ASSERT_TRUE(pending_request.has_value());
    os_kernel.tlb_shootdown_controller().apply(target_mmu, pending_request.value());

    EXPECT_TRUE(os_kernel.tlb_shootdown_controller().has_acknowledged(sequence));
    EXPECT_EQ(target_mmu.tlb().lookup(TEST_LINEAR_PAGE, TEST_TLB_GENERATION, TEST_PCID), nullptr);
}
