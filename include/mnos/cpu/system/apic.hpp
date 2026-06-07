#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <mnos/cpu/system/core_topology.hpp>
#include <mnos/cpu/system/interrupt_vector.hpp>

namespace mnos::cpu::system
{
inline constexpr std::uint64_t APIC_TIMER_MIN_INTERVAL_TICKS = std::uint64_t{1};
inline constexpr std::size_t IO_APIC_IRQ_LINE_COUNT = 24;

enum class ApicInterruptKind : std::uint8_t
{
    TIMER,
    IPI,
    EXTERNAL_IRQ,
};

enum class ApicTimerMode : std::uint8_t
{
    ONE_SHOT,
    PERIODIC,
};

class ApicInterrupt final
{
public:
    [[nodiscard]] static ApicInterrupt timer(CoreId target_core, InterruptVector vector) noexcept;
    [[nodiscard]] static ApicInterrupt ipi(CoreId source_core, CoreId target_core, InterruptVector vector) noexcept;
    [[nodiscard]] static ApicInterrupt external_irq(
        std::uint8_t irq_line,
        CoreId target_core,
        InterruptVector vector) noexcept;

    [[nodiscard]] ApicInterruptKind kind() const noexcept;
    [[nodiscard]] InterruptVector vector() const noexcept;
    [[nodiscard]] CoreId target_core() const noexcept;
    [[nodiscard]] std::optional<CoreId> source_core() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> irq_line() const noexcept;

private:
    ApicInterrupt(
        ApicInterruptKind kind,
        InterruptVector vector,
        CoreId target_core,
        std::optional<CoreId> source_core,
        std::optional<std::uint8_t> irq_line) noexcept;

    ApicInterruptKind kind_;
    InterruptVector vector_;
    CoreId target_core_;
    std::optional<CoreId> source_core_;
    std::optional<std::uint8_t> irq_line_;
};

class LocalApicTimer final
{
public:
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] ApicTimerMode mode() const noexcept;
    [[nodiscard]] InterruptVector vector() const noexcept;
    [[nodiscard]] std::uint64_t initial_count() const noexcept;
    [[nodiscard]] std::uint64_t remaining_count() const noexcept;
    [[nodiscard]] std::uint64_t elapsed_ticks() const noexcept;

    void configure(InterruptVector vector, std::uint64_t interval_ticks, ApicTimerMode mode);
    void disable() noexcept;
    [[nodiscard]] std::optional<InterruptVector> tick() noexcept;

private:
    bool enabled_ = false;
    ApicTimerMode mode_ = ApicTimerMode::ONE_SHOT;
    InterruptVector vector_ = InterruptVector::timer();
    std::uint64_t initial_count_ = APIC_TIMER_MIN_INTERVAL_TICKS;
    std::uint64_t remaining_count_ = APIC_TIMER_MIN_INTERVAL_TICKS;
    std::uint64_t elapsed_ticks_ = std::uint64_t{0};
};

class LocalApic final
{
public:
    explicit LocalApic(CoreId core_id) noexcept;

    [[nodiscard]] CoreId core_id() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    void enable() noexcept;
    void disable() noexcept;

    [[nodiscard]] LocalApicTimer& timer() noexcept;
    [[nodiscard]] const LocalApicTimer& timer() const noexcept;
    void configure_periodic_timer(InterruptVector vector, std::uint64_t interval_ticks);
    void configure_one_shot_timer(InterruptVector vector, std::uint64_t interval_ticks);

    [[nodiscard]] std::size_t pending_interrupt_count() const noexcept;
    [[nodiscard]] std::optional<ApicInterrupt> tick() noexcept;
    [[nodiscard]] bool receive_interrupt(const ApicInterrupt& interrupt);
    [[nodiscard]] std::optional<ApicInterrupt> take_pending_interrupt();

private:
    CoreId core_id_;
    bool enabled_ = false;
    LocalApicTimer timer_;
    std::deque<ApicInterrupt> pending_interrupts_;
};

class IoApicRoute final
{
public:
    IoApicRoute() noexcept = default;
    IoApicRoute(InterruptVector vector, CoreId target_core, bool masked) noexcept;

    [[nodiscard]] InterruptVector vector() const noexcept;
    [[nodiscard]] CoreId target_core() const noexcept;
    [[nodiscard]] bool masked() const noexcept;
    [[nodiscard]] IoApicRoute with_masked(bool masked) const noexcept;

private:
    InterruptVector vector_ = InterruptVector::timer();
    CoreId target_core_ = CoreId::bootstrap();
    bool masked_ = false;
};

class IoApic final
{
public:
    [[nodiscard]] bool has_route(std::uint8_t irq_line) const;
    [[nodiscard]] const IoApicRoute& route_at(std::uint8_t irq_line) const;
    void set_route(std::uint8_t irq_line, InterruptVector vector, CoreId target_core);
    void clear_route(std::uint8_t irq_line);
    void mask_irq(std::uint8_t irq_line);
    void unmask_irq(std::uint8_t irq_line);
    [[nodiscard]] std::optional<ApicInterrupt> raise_irq(std::uint8_t irq_line) const;

private:
    struct RouteSlot
    {
        IoApicRoute route;
        bool present = false;
    };

    [[nodiscard]] static std::size_t irq_index(std::uint8_t irq_line);

    std::array<RouteSlot, IO_APIC_IRQ_LINE_COUNT> routes_{};
};

class ApicSystem final
{
public:
    explicit ApicSystem(CoreTopology topology);

    [[nodiscard]] const CoreTopology& topology() const noexcept;
    [[nodiscard]] std::size_t local_apic_count() const noexcept;
    [[nodiscard]] LocalApic& local_apic(CoreId core_id);
    [[nodiscard]] const LocalApic& local_apic(CoreId core_id) const;
    [[nodiscard]] IoApic& io_apic() noexcept;
    [[nodiscard]] const IoApic& io_apic() const noexcept;

    void enable_local_apics() noexcept;
    [[nodiscard]] std::optional<ApicInterrupt> tick(CoreId core_id);
    [[nodiscard]] bool send_ipi(CoreId source_core, CoreId target_core, InterruptVector vector);
    [[nodiscard]] std::size_t broadcast_ipi(CoreId source_core, InterruptVector vector);
    [[nodiscard]] std::optional<ApicInterrupt> raise_irq(std::uint8_t irq_line);
    [[nodiscard]] std::optional<ApicInterrupt> take_pending_interrupt(CoreId core_id);

private:
    [[nodiscard]] std::size_t local_apic_index(CoreId core_id) const;

    CoreTopology topology_;
    std::vector<LocalApic> local_apics_;
    IoApic io_apic_;
};
}
