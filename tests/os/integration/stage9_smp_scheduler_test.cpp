#include <cstddef>
#include <optional>
#include <stdexcept>

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

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{128});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{3};
constexpr cpu_system::CoreId TEST_CORE_0{0};
constexpr cpu_system::CoreId TEST_CORE_1{1};
constexpr cpu_system::CoreId TEST_CORE_2{2};
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

TEST(Stage9IntegrationTest, SmpTimerWakeMigrationAndShootdownFlowAcrossCores)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    ASSERT_TRUE(os_kernel.has_stage9_services());

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& first_thread = os_kernel.create_thread_on_core(process, TEST_CORE_0);
    sched::ThreadContext& second_thread = os_kernel.create_thread_on_core(process, TEST_CORE_0);
    sched::ThreadContext& third_thread = os_kernel.create_thread_on_core(process, TEST_CORE_1);

    ASSERT_NE(os_kernel.smp_scheduler().schedule_next(TEST_CORE_0), nullptr);
    ASSERT_NE(os_kernel.smp_scheduler().schedule_next(TEST_CORE_1), nullptr);
    EXPECT_THAT(os_kernel.smp_scheduler().current(TEST_CORE_0).id(), Eq(first_thread.id()));
    EXPECT_THAT(os_kernel.smp_scheduler().current(TEST_CORE_1).id(), Eq(third_thread.id()));

    const std::optional<cpu_system::ApicInterrupt> timer_interrupt = os_kernel.tick_core_timer(TEST_CORE_0);
    ASSERT_TRUE(timer_interrupt.has_value());
    EXPECT_THAT(timer_interrupt->kind(), Eq(cpu_system::ApicInterruptKind::TIMER));
    EXPECT_THAT(os_kernel.smp_scheduler().current(TEST_CORE_0).id(), Eq(second_thread.id()));
    EXPECT_THAT(os_kernel.smp_scheduler().current(TEST_CORE_1).id(), Eq(third_thread.id()));
    EXPECT_THAT(os_kernel.smp_scheduler().core_preemption_count(TEST_CORE_0), Eq(std::size_t{1}));

    ASSERT_EQ(os_kernel.smp_scheduler().block_current(TEST_CORE_1), nullptr);
    EXPECT_THAT(third_thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_TRUE(os_kernel.wake_thread_on_core(TEST_CORE_0, TEST_CORE_2, third_thread));
    const std::optional<cpu_system::ApicInterrupt> wake_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_CORE_2);
    ASSERT_TRUE(wake_ipi.has_value());
    EXPECT_THAT(wake_ipi->vector(), Eq(cpu_system::InterruptVector::reschedule()));
    ASSERT_NE(os_kernel.smp_scheduler().schedule_next(TEST_CORE_2), nullptr);
    EXPECT_THAT(os_kernel.smp_scheduler().current(TEST_CORE_2).id(), Eq(third_thread.id()));

    EXPECT_THROW(
        static_cast<void>(os_kernel.request_smp_migration(TEST_CORE_1, TEST_CORE_2, first_thread)),
        std::logic_error);
    EXPECT_THAT(os_kernel.scheduler_handoff_count(), Eq(std::size_t{0}));
    ASSERT_TRUE(os_kernel.smp_scheduler().current_core_of(first_thread).has_value());
    EXPECT_THAT(*os_kernel.smp_scheduler().current_core_of(first_thread), Eq(TEST_CORE_0));

    const kernel::SchedulerHandoff& handoff =
        os_kernel.request_smp_migration(TEST_CORE_0, TEST_CORE_2, first_thread);
    EXPECT_THAT(handoff.source_core(), Eq(TEST_CORE_0));
    EXPECT_THAT(handoff.target_core(), Eq(TEST_CORE_2));
    EXPECT_THAT(handoff.thread_id(), Eq(first_thread.id()));
    EXPECT_THAT(first_thread.cpu_state().core_id(), Eq(TEST_CORE_2));
    const std::optional<cpu_system::ApicInterrupt> migration_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_CORE_2);
    ASSERT_TRUE(migration_ipi.has_value());
    EXPECT_THAT(migration_ipi->vector(), Eq(cpu_system::InterruptVector::reschedule()));

    cpu_memory::MemoryManagementUnit target_mmu;
    target_mmu.tlb().insert(make_translation(), TEST_TLB_GENERATION, TEST_PCID);
    const cpu_memory::TlbShootdownRequest& request = os_kernel.request_tlb_shootdown_page(
        TEST_CORE_0,
        TEST_CORE_1,
        TEST_LINEAR_PAGE,
        TEST_PCID);
    const std::optional<cpu_system::ApicInterrupt> shootdown_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_CORE_1);
    ASSERT_TRUE(shootdown_ipi.has_value());
    EXPECT_THAT(shootdown_ipi->vector(), Eq(cpu_system::InterruptVector::tlb_shootdown()));

    EXPECT_TRUE(os_kernel.apply_next_tlb_shootdown_for_core(TEST_CORE_1, target_mmu));
    EXPECT_TRUE(os_kernel.tlb_shootdown_controller().has_acknowledged(request.sequence()));
    EXPECT_EQ(target_mmu.tlb().lookup(TEST_LINEAR_PAGE, TEST_TLB_GENERATION, TEST_PCID), nullptr);
    EXPECT_FALSE(os_kernel.apply_next_tlb_shootdown_for_core(TEST_CORE_1, target_mmu));
}

TEST(Stage9IntegrationTest, RebalanceRecordsMigrationHandoffAndRescheduleIpi)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();

    static_cast<void>(os_kernel.create_thread_on_core(process, TEST_CORE_0));
    static_cast<void>(os_kernel.create_thread_on_core(process, TEST_CORE_0));
    static_cast<void>(os_kernel.create_thread_on_core(process, TEST_CORE_0));
    static_cast<void>(os_kernel.create_thread_on_core(process, TEST_CORE_0));

    const std::optional<sched::ThreadMigration> migration = os_kernel.rebalance_smp_once();
    ASSERT_TRUE(migration.has_value());
    EXPECT_THAT(migration->source_core(), Eq(TEST_CORE_0));
    EXPECT_FALSE(migration->target_core() == TEST_CORE_0);
    EXPECT_THAT(os_kernel.scheduler_handoff_count(), Eq(std::size_t{1}));

    const std::optional<cpu_system::ApicInterrupt> reschedule_ipi =
        os_kernel.apic_system().take_pending_interrupt(migration->target_core());
    ASSERT_TRUE(reschedule_ipi.has_value());
    EXPECT_THAT(reschedule_ipi->vector(), Eq(cpu_system::InterruptVector::reschedule()));
}
