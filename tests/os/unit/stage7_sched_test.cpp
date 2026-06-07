#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/system/interrupt_vector.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/platform/machine.hpp>
#include <mnos/os/sched/sleep_queue.hpp>
#include <mnos/os/sched/wait_queue.hpp>

namespace cpu = mnos::cpu;
namespace cpu_system = mnos::cpu::system;
namespace kernel = mnos::os::kernel;
namespace mm = mnos::os::mm;
namespace platform = mnos::os::platform;
namespace proc = mnos::os::proc;
namespace sched = mnos::os::sched;

namespace
{
using ::testing::Eq;

constexpr std::size_t TEST_MEMORY_SIZE_BYTES = static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{64});
constexpr std::uint32_t TEST_PROCESSOR_COUNT = std::uint32_t{2};
constexpr mm::VirtualAddress TEST_STACK_A{mm::AddressValue{0x100000}};
constexpr mm::VirtualAddress TEST_STACK_B{mm::AddressValue{0x110000}};
constexpr mm::VirtualAddress TEST_STACK_C{mm::AddressValue{0x120000}};
constexpr sched::SchedulerTick TEST_WAKE_TICK_EARLY = sched::SchedulerTick{3};
constexpr sched::SchedulerTick TEST_WAKE_TICK_LATE = sched::SchedulerTick{5};
constexpr sched::SchedulerTick TEST_SLEEP_DURATION_TICKS = sched::SchedulerTick{2};
constexpr cpu_system::CoreId TEST_BOOT_CORE{0};
constexpr cpu_system::CoreId TEST_SECOND_CORE{1};
constexpr cpu_system::CoreId TEST_INVALID_CORE{99};
constexpr cpu::Address64 TEST_SHOOTDOWN_ADDRESS = cpu::Address64{0x4000};
}

TEST(SleepQueueTest, OrdersSleepersAndSkipsDeadThreads)
{
    sched::ThreadContext first_thread{sched::ThreadId{1}, TEST_STACK_A};
    sched::ThreadContext second_thread{sched::ThreadId{2}, TEST_STACK_B};
    sched::ThreadContext third_thread{sched::ThreadId{3}, TEST_STACK_C};
    sched::SleepQueue sleep_queue;

    sleep_queue.sleep_until(first_thread, TEST_WAKE_TICK_LATE);
    sleep_queue.sleep_until(second_thread, TEST_WAKE_TICK_EARLY);
    sleep_queue.sleep_until(third_thread, TEST_WAKE_TICK_LATE);

    EXPECT_THAT(sleep_queue.size(), Eq(std::size_t{3}));
    ASSERT_TRUE(sleep_queue.next_wake_tick().has_value());
    EXPECT_THAT(sleep_queue.next_wake_tick().value(), Eq(TEST_WAKE_TICK_EARLY));
    EXPECT_TRUE(sleep_queue.contains(first_thread));
    EXPECT_THROW(sleep_queue.sleep_until(first_thread, TEST_WAKE_TICK_LATE), std::logic_error);

    std::vector<sched::ThreadContext*> ready_threads = sleep_queue.take_ready(sched::SchedulerTick{4});
    ASSERT_THAT(ready_threads.size(), Eq(std::size_t{1}));
    EXPECT_THAT(ready_threads.front()->id(), Eq(second_thread.id()));
    EXPECT_THAT(sleep_queue.size(), Eq(std::size_t{2}));

    first_thread.set_state(sched::ThreadState::DEAD);
    ready_threads = sleep_queue.take_ready(TEST_WAKE_TICK_LATE);
    ASSERT_THAT(ready_threads.size(), Eq(std::size_t{1}));
    EXPECT_THAT(ready_threads.front()->id(), Eq(third_thread.id()));
    EXPECT_TRUE(sleep_queue.empty());
}

TEST(WaitQueueTest, WakesOneOrAllWaitersWithoutOwningThreads)
{
    sched::ThreadContext first_thread{sched::ThreadId{1}, TEST_STACK_A};
    sched::ThreadContext second_thread{sched::ThreadId{2}, TEST_STACK_B};
    sched::ThreadContext third_thread{sched::ThreadId{3}, TEST_STACK_C};
    sched::WaitQueue wait_queue;

    wait_queue.wait(first_thread);
    wait_queue.wait(second_thread);
    EXPECT_TRUE(wait_queue.contains(first_thread));
    EXPECT_THAT(wait_queue.size(), Eq(std::size_t{2}));
    EXPECT_THROW(wait_queue.wait(first_thread), std::logic_error);

    sched::ThreadContext* const first_ready_thread = wait_queue.wake_one();
    ASSERT_NE(first_ready_thread, nullptr);
    EXPECT_THAT(first_ready_thread->id(), Eq(first_thread.id()));

    wait_queue.wait(third_thread);
    third_thread.set_state(sched::ThreadState::DEAD);
    const std::vector<sched::ThreadContext*> remaining_ready_threads = wait_queue.wake_all();
    ASSERT_THAT(remaining_ready_threads.size(), Eq(std::size_t{1}));
    EXPECT_THAT(remaining_ready_threads.front()->id(), Eq(second_thread.id()));
    EXPECT_TRUE(wait_queue.empty());
    EXPECT_EQ(wait_queue.wake_one(), nullptr);
}

TEST(KernelStage7Test, TimerTickPreemptsCurrentThreadAndWakesSleepers)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& first_thread = os_kernel.create_thread(process);
    sched::ThreadContext& second_thread = os_kernel.create_thread(process);

    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(first_thread.id()));
    ASSERT_NE(os_kernel.sleep_current_for(TEST_SLEEP_DURATION_TICKS), nullptr);
    EXPECT_THAT(first_thread.state(), Eq(sched::ThreadState::BLOCKED));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(second_thread.id()));
    EXPECT_THAT(os_kernel.sleep_queue().size(), Eq(std::size_t{1}));

    const std::optional<cpu_system::ApicInterrupt> first_timer = os_kernel.tick_core_timer(TEST_BOOT_CORE);
    ASSERT_TRUE(first_timer.has_value());
    EXPECT_THAT(os_kernel.scheduler_tick_count(), Eq(sched::SchedulerTick{1}));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(second_thread.id()));

    const std::optional<cpu_system::ApicInterrupt> second_timer = os_kernel.tick_core_timer(TEST_BOOT_CORE);
    ASSERT_TRUE(second_timer.has_value());
    EXPECT_THAT(os_kernel.scheduler_tick_count(), Eq(sched::SchedulerTick{2}));
    EXPECT_THAT(os_kernel.scheduler().current().id(), Eq(first_thread.id()));
    EXPECT_THAT(first_thread.state(), Eq(sched::ThreadState::RUNNING));
}

TEST(KernelStage7Test, GuardsAndConstAccessorsExposeStage7Services)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    const kernel::Kernel& const_kernel = os_kernel;

    EXPECT_FALSE(os_kernel.has_stage7_services());
    EXPECT_THROW(static_cast<void>(os_kernel.apic_system()), std::logic_error);
    EXPECT_THROW(static_cast<void>(const_kernel.apic_system()), std::logic_error);

    os_kernel.boot();
    EXPECT_TRUE(os_kernel.has_stage7_services());
    EXPECT_THAT(const_kernel.apic_system().local_apic_count(), Eq(static_cast<std::size_t>(TEST_PROCESSOR_COUNT)));
    EXPECT_TRUE(const_kernel.sleep_queue().empty());
    EXPECT_THAT(const_kernel.tlb_shootdown_controller().pending_count(), Eq(std::size_t{0}));
}

TEST(KernelStage7Test, HandlesNoCurrentAndStage7ErrorBranches)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();

    EXPECT_EQ(os_kernel.sleep_current_until(sched::SchedulerTick{1}), nullptr);
    EXPECT_EQ(os_kernel.handle_timer_interrupt(TEST_BOOT_CORE), nullptr);
    EXPECT_THROW(
        static_cast<void>(os_kernel.sleep_current_for(std::numeric_limits<sched::SchedulerTick>::max())),
        std::overflow_error);

    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& current_thread = os_kernel.create_thread(process);
    ASSERT_NE(os_kernel.scheduler().schedule_next(), nullptr);
    EXPECT_THAT(os_kernel.sleep_current_until(os_kernel.scheduler_tick_count()), Eq(&current_thread));

    const cpu::memory::TlbShootdownRequest& request =
        os_kernel.request_tlb_shootdown_all(TEST_BOOT_CORE, TEST_SECOND_CORE);
    EXPECT_THAT(request.scope(), Eq(cpu::memory::TlbShootdownScope::ALL));
    EXPECT_THAT(os_kernel.tlb_shootdown_controller().pending_count(), Eq(std::size_t{1}));
    EXPECT_THROW(
        static_cast<void>(
            os_kernel.request_tlb_shootdown_page(TEST_BOOT_CORE, TEST_INVALID_CORE, TEST_SHOOTDOWN_ADDRESS)),
        std::out_of_range);
    EXPECT_THAT(os_kernel.tlb_shootdown_controller().pending_count(), Eq(std::size_t{1}));

    sched::ThreadContext dead_thread{sched::ThreadId{99}, TEST_STACK_C};
    dead_thread.set_state(sched::ThreadState::DEAD);
    EXPECT_THROW(
        static_cast<void>(os_kernel.request_scheduler_handoff(TEST_BOOT_CORE, TEST_SECOND_CORE, dead_thread)),
        std::logic_error);
}

TEST(KernelStage7Test, RequestsTlbShootdownIpiAndSchedulerHandoff)
{
    platform::Machine machine(TEST_MEMORY_SIZE_BYTES, TEST_PROCESSOR_COUNT);
    kernel::BootContext boot_context{machine, TEST_PROCESSOR_COUNT};
    kernel::Kernel os_kernel{boot_context};
    os_kernel.boot();
    proc::Process& process = os_kernel.create_process();
    sched::ThreadContext& thread = os_kernel.create_thread(process);

    const cpu::memory::TlbShootdownRequest& request =
        os_kernel.request_tlb_shootdown_page(TEST_BOOT_CORE, TEST_SECOND_CORE, TEST_SHOOTDOWN_ADDRESS);
    EXPECT_THAT(request.target_core(), Eq(TEST_SECOND_CORE));
    EXPECT_THAT(os_kernel.tlb_shootdown_controller().pending_count(), Eq(std::size_t{1}));
    const std::optional<cpu_system::ApicInterrupt> shootdown_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_SECOND_CORE);
    ASSERT_TRUE(shootdown_ipi.has_value());
    EXPECT_THAT(shootdown_ipi->vector(), Eq(cpu_system::InterruptVector::tlb_shootdown()));

    const kernel::SchedulerHandoff& handoff =
        os_kernel.request_scheduler_handoff(TEST_BOOT_CORE, TEST_SECOND_CORE, thread);
    EXPECT_THAT(handoff.sequence(), Eq(kernel::KERNEL_SCHEDULER_HANDOFF_FIRST_SEQUENCE));
    EXPECT_THAT(handoff.source_core(), Eq(TEST_BOOT_CORE));
    EXPECT_THAT(handoff.thread_id(), Eq(thread.id()));
    EXPECT_THAT(thread.cpu_state().core_id(), Eq(TEST_SECOND_CORE));
    EXPECT_THAT(os_kernel.scheduler_handoff_count(), Eq(std::size_t{1}));
    EXPECT_THAT(os_kernel.scheduler_handoff_at(std::size_t{0}).target_core(), Eq(TEST_SECOND_CORE));

    const std::optional<cpu_system::ApicInterrupt> reschedule_ipi =
        os_kernel.apic_system().take_pending_interrupt(TEST_SECOND_CORE);
    ASSERT_TRUE(reschedule_ipi.has_value());
    EXPECT_THAT(reschedule_ipi->vector(), Eq(cpu_system::InterruptVector::reschedule()));
    EXPECT_THROW(static_cast<void>(os_kernel.scheduler_handoff_at(std::size_t{1})), std::out_of_range);
}
