#include <cstdint>
#include <optional>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/system/apic.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>

namespace cpu_system = mnos::cpu::system;

namespace
{
using ::testing::Eq;

constexpr cpu_system::CoreId TEST_BOOT_CORE{0};
constexpr cpu_system::CoreId TEST_SECOND_CORE{1};
constexpr cpu_system::CoreId TEST_THIRD_CORE{2};
constexpr cpu_system::CoreId TEST_OUT_OF_RANGE_CORE{3};
constexpr std::uint64_t TEST_PERIODIC_INTERVAL_TICKS = std::uint64_t{2};
constexpr std::uint64_t TEST_ONE_SHOT_INTERVAL_TICKS = std::uint64_t{1};
constexpr std::uint32_t TEST_CORE_COUNT = std::uint32_t{3};
constexpr std::uint8_t TEST_IRQ_LINE = std::uint8_t{2};
constexpr std::uint8_t TEST_OUT_OF_RANGE_IRQ_LINE =
    static_cast<std::uint8_t>(cpu_system::IO_APIC_IRQ_LINE_COUNT);
constexpr cpu_system::InterruptVector TEST_EXTERNAL_VECTOR{45};
}

TEST(ApicLocalTimerTest, PeriodicAndOneShotTimersQueueInterrupts)
{
    cpu_system::LocalApic local_apic{TEST_SECOND_CORE};
    local_apic.configure_periodic_timer(cpu_system::InterruptVector::timer(), TEST_PERIODIC_INTERVAL_TICKS);

    EXPECT_FALSE(local_apic.tick().has_value());
    local_apic.enable();
    EXPECT_FALSE(local_apic.tick().has_value());

    const std::optional<cpu_system::ApicInterrupt> timer_interrupt = local_apic.tick();
    ASSERT_TRUE(timer_interrupt.has_value());
    EXPECT_THAT(timer_interrupt->kind(), Eq(cpu_system::ApicInterruptKind::TIMER));
    EXPECT_THAT(timer_interrupt->target_core(), Eq(TEST_SECOND_CORE));
    ASSERT_TRUE(timer_interrupt->source_core().has_value());
    EXPECT_THAT(timer_interrupt->source_core().value(), Eq(TEST_SECOND_CORE));
    EXPECT_THAT(local_apic.pending_interrupt_count(), Eq(std::size_t{1}));

    const std::optional<cpu_system::ApicInterrupt> pending_interrupt = local_apic.take_pending_interrupt();
    ASSERT_TRUE(pending_interrupt.has_value());
    EXPECT_THAT(pending_interrupt->vector(), Eq(cpu_system::InterruptVector::timer()));

    local_apic.configure_one_shot_timer(cpu_system::InterruptVector::reschedule(), TEST_ONE_SHOT_INTERVAL_TICKS);
    const std::optional<cpu_system::ApicInterrupt> one_shot_interrupt = local_apic.tick();
    ASSERT_TRUE(one_shot_interrupt.has_value());
    EXPECT_THAT(one_shot_interrupt->vector(), Eq(cpu_system::InterruptVector::reschedule()));
    EXPECT_FALSE(local_apic.timer().enabled());

    EXPECT_THROW(
        local_apic.configure_periodic_timer(cpu_system::InterruptVector::timer(), std::uint64_t{0}),
        std::invalid_argument);
}

TEST(ApicLocalTimerTest, ExposesConfigurationAndDisabledState)
{
    cpu_system::LocalApicTimer timer;
    EXPECT_FALSE(timer.enabled());
    EXPECT_FALSE(timer.tick().has_value());

    timer.configure(
        cpu_system::InterruptVector::reschedule(),
        TEST_PERIODIC_INTERVAL_TICKS,
        cpu_system::ApicTimerMode::PERIODIC);
    EXPECT_TRUE(timer.enabled());
    EXPECT_THAT(timer.mode(), Eq(cpu_system::ApicTimerMode::PERIODIC));
    EXPECT_THAT(timer.vector(), Eq(cpu_system::InterruptVector::reschedule()));
    EXPECT_THAT(timer.initial_count(), Eq(TEST_PERIODIC_INTERVAL_TICKS));
    EXPECT_THAT(timer.remaining_count(), Eq(TEST_PERIODIC_INTERVAL_TICKS));

    EXPECT_FALSE(timer.tick().has_value());
    EXPECT_THAT(timer.elapsed_ticks(), Eq(std::uint64_t{1}));
    EXPECT_THAT(timer.remaining_count(), Eq(std::uint64_t{1}));
    timer.disable();
    EXPECT_FALSE(timer.enabled());
    EXPECT_FALSE(timer.tick().has_value());
}

TEST(ApicLocalApicTest, RejectsMismatchedTargetsAndHonorsDisabledDelivery)
{
    cpu_system::LocalApic local_apic{TEST_SECOND_CORE};
    const cpu_system::ApicInterrupt correct_target =
        cpu_system::ApicInterrupt::ipi(TEST_BOOT_CORE, TEST_SECOND_CORE, cpu_system::InterruptVector::reschedule());
    const cpu_system::ApicInterrupt wrong_target =
        cpu_system::ApicInterrupt::ipi(TEST_BOOT_CORE, TEST_THIRD_CORE, cpu_system::InterruptVector::reschedule());

    EXPECT_FALSE(local_apic.enabled());
    EXPECT_FALSE(local_apic.receive_interrupt(correct_target));
    local_apic.enable();
    EXPECT_TRUE(local_apic.enabled());
    EXPECT_THROW(static_cast<void>(local_apic.receive_interrupt(wrong_target)), std::logic_error);
    EXPECT_FALSE(local_apic.take_pending_interrupt().has_value());

    EXPECT_TRUE(local_apic.receive_interrupt(correct_target));
    local_apic.disable();
    EXPECT_FALSE(local_apic.enabled());
    EXPECT_FALSE(local_apic.take_pending_interrupt().has_value());
}

TEST(ApicSystemTest, SendsSingleAndBroadcastIpis)
{
    cpu_system::ApicSystem apic_system{cpu_system::CoreTopology{TEST_CORE_COUNT}};
    apic_system.enable_local_apics();

    EXPECT_TRUE(apic_system.send_ipi(
        TEST_BOOT_CORE,
        TEST_SECOND_CORE,
        cpu_system::InterruptVector::reschedule()));
    const std::optional<cpu_system::ApicInterrupt> single_ipi =
        apic_system.take_pending_interrupt(TEST_SECOND_CORE);
    ASSERT_TRUE(single_ipi.has_value());
    EXPECT_THAT(single_ipi->kind(), Eq(cpu_system::ApicInterruptKind::IPI));
    ASSERT_TRUE(single_ipi->source_core().has_value());
    EXPECT_THAT(single_ipi->source_core().value(), Eq(TEST_BOOT_CORE));
    EXPECT_THAT(single_ipi->target_core(), Eq(TEST_SECOND_CORE));

    EXPECT_THAT(
        apic_system.broadcast_ipi(TEST_BOOT_CORE, cpu_system::InterruptVector::tlb_shootdown()),
        Eq(std::size_t{2}));
    EXPECT_THAT(apic_system.take_pending_interrupt(TEST_SECOND_CORE)->vector(), Eq(cpu_system::InterruptVector::tlb_shootdown()));
    EXPECT_THAT(apic_system.take_pending_interrupt(TEST_THIRD_CORE)->vector(), Eq(cpu_system::InterruptVector::tlb_shootdown()));
    EXPECT_THROW(
        static_cast<void>(apic_system.send_ipi(TEST_BOOT_CORE, TEST_OUT_OF_RANGE_CORE, cpu_system::InterruptVector::timer())),
        std::out_of_range);
}

TEST(IoApicTest, RoutesMasksAndRaisesExternalInterrupts)
{
    cpu_system::IoApic io_apic;
    EXPECT_FALSE(io_apic.raise_irq(TEST_IRQ_LINE).has_value());
    EXPECT_FALSE(io_apic.has_route(TEST_IRQ_LINE));

    io_apic.set_route(TEST_IRQ_LINE, TEST_EXTERNAL_VECTOR, TEST_SECOND_CORE);
    EXPECT_TRUE(io_apic.has_route(TEST_IRQ_LINE));
    EXPECT_THAT(io_apic.route_at(TEST_IRQ_LINE).target_core(), Eq(TEST_SECOND_CORE));

    std::optional<cpu_system::ApicInterrupt> external_interrupt = io_apic.raise_irq(TEST_IRQ_LINE);
    ASSERT_TRUE(external_interrupt.has_value());
    EXPECT_THAT(external_interrupt->kind(), Eq(cpu_system::ApicInterruptKind::EXTERNAL_IRQ));
    EXPECT_THAT(external_interrupt->vector(), Eq(TEST_EXTERNAL_VECTOR));
    ASSERT_TRUE(external_interrupt->irq_line().has_value());
    EXPECT_THAT(external_interrupt->irq_line().value(), Eq(TEST_IRQ_LINE));

    io_apic.mask_irq(TEST_IRQ_LINE);
    EXPECT_FALSE(io_apic.raise_irq(TEST_IRQ_LINE).has_value());
    io_apic.unmask_irq(TEST_IRQ_LINE);
    EXPECT_TRUE(io_apic.raise_irq(TEST_IRQ_LINE).has_value());
    io_apic.clear_route(TEST_IRQ_LINE);
    EXPECT_FALSE(io_apic.has_route(TEST_IRQ_LINE));
    EXPECT_THROW(static_cast<void>(io_apic.route_at(TEST_IRQ_LINE)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(io_apic.has_route(TEST_OUT_OF_RANGE_IRQ_LINE)), std::out_of_range);
    EXPECT_THROW(io_apic.mask_irq(TEST_IRQ_LINE), std::out_of_range);
    EXPECT_THROW(io_apic.unmask_irq(TEST_IRQ_LINE), std::out_of_range);
}

TEST(ApicSystemTest, DeliversIoApicInterruptsToTargetLocalApic)
{
    cpu_system::ApicSystem apic_system{cpu_system::CoreTopology{TEST_CORE_COUNT}};
    apic_system.enable_local_apics();
    apic_system.io_apic().set_route(TEST_IRQ_LINE, TEST_EXTERNAL_VECTOR, TEST_THIRD_CORE);

    const std::optional<cpu_system::ApicInterrupt> delivered_interrupt = apic_system.raise_irq(TEST_IRQ_LINE);
    ASSERT_TRUE(delivered_interrupt.has_value());
    EXPECT_THAT(delivered_interrupt->target_core(), Eq(TEST_THIRD_CORE));

    const std::optional<cpu_system::ApicInterrupt> pending_interrupt =
        apic_system.take_pending_interrupt(TEST_THIRD_CORE);
    ASSERT_TRUE(pending_interrupt.has_value());
    EXPECT_THAT(pending_interrupt->kind(), Eq(cpu_system::ApicInterruptKind::EXTERNAL_IRQ));
    EXPECT_THAT(pending_interrupt->vector(), Eq(TEST_EXTERNAL_VECTOR));
}

TEST(ApicSystemTest, ExposesConstAccessorsAndDropsDeliveryToDisabledTargets)
{
    cpu_system::ApicSystem apic_system{cpu_system::CoreTopology{TEST_CORE_COUNT}};
    const cpu_system::ApicSystem& const_apic_system = apic_system;
    EXPECT_THAT(const_apic_system.local_apic_count(), Eq(static_cast<std::size_t>(TEST_CORE_COUNT)));
    EXPECT_THAT(const_apic_system.local_apic(TEST_SECOND_CORE).core_id(), Eq(TEST_SECOND_CORE));
    EXPECT_FALSE(const_apic_system.io_apic().has_route(TEST_IRQ_LINE));

    apic_system.io_apic().set_route(TEST_IRQ_LINE, TEST_EXTERNAL_VECTOR, TEST_SECOND_CORE);
    EXPECT_FALSE(apic_system.raise_irq(TEST_IRQ_LINE).has_value());
    apic_system.enable_local_apics();
    EXPECT_TRUE(apic_system.raise_irq(TEST_IRQ_LINE).has_value());
    EXPECT_FALSE(apic_system.raise_irq(std::uint8_t{0}).has_value());
}
