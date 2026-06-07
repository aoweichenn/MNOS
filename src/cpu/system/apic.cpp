#include <stdexcept>

#include <mnos/cpu/system/apic.hpp>

namespace
{
constexpr const char* APIC_TIMER_ZERO_INTERVAL_MESSAGE = "local apic timer interval must be non-zero";
constexpr const char* LOCAL_APIC_TARGET_MISMATCH_MESSAGE = "local apic interrupt target does not match this core";
constexpr const char* IO_APIC_IRQ_OUT_OF_RANGE_MESSAGE = "io apic irq line is out of range";
constexpr const char* IO_APIC_ROUTE_NOT_PRESENT_MESSAGE = "io apic irq route is not present";
constexpr const char* APIC_CORE_OUT_OF_RANGE_MESSAGE = "apic core id is outside the topology";
}

namespace mnos::cpu::system
{
ApicInterrupt ApicInterrupt::timer(const CoreId target_core, const InterruptVector vector) noexcept
{
    return ApicInterrupt{ApicInterruptKind::TIMER, vector, target_core, target_core, std::nullopt};
}

ApicInterrupt ApicInterrupt::ipi(
    const CoreId source_core,
    const CoreId target_core,
    const InterruptVector vector) noexcept
{
    return ApicInterrupt{ApicInterruptKind::IPI, vector, target_core, source_core, std::nullopt};
}

ApicInterrupt ApicInterrupt::external_irq(
    const std::uint8_t irq_line,
    const CoreId target_core,
    const InterruptVector vector) noexcept
{
    return ApicInterrupt{ApicInterruptKind::EXTERNAL_IRQ, vector, target_core, std::nullopt, irq_line};
}

ApicInterrupt::ApicInterrupt(
    const ApicInterruptKind kind,
    const InterruptVector vector,
    const CoreId target_core,
    std::optional<CoreId> source_core,
    std::optional<std::uint8_t> irq_line) noexcept :
    kind_(kind),
    vector_(vector), target_core_(target_core), source_core_(source_core), irq_line_(irq_line)
{
}

ApicInterruptKind ApicInterrupt::kind() const noexcept
{
    return this->kind_;
}

InterruptVector ApicInterrupt::vector() const noexcept
{
    return this->vector_;
}

CoreId ApicInterrupt::target_core() const noexcept
{
    return this->target_core_;
}

std::optional<CoreId> ApicInterrupt::source_core() const noexcept
{
    return this->source_core_;
}

std::optional<std::uint8_t> ApicInterrupt::irq_line() const noexcept
{
    return this->irq_line_;
}

bool LocalApicTimer::enabled() const noexcept
{
    return this->enabled_;
}

ApicTimerMode LocalApicTimer::mode() const noexcept
{
    return this->mode_;
}

InterruptVector LocalApicTimer::vector() const noexcept
{
    return this->vector_;
}

std::uint64_t LocalApicTimer::initial_count() const noexcept
{
    return this->initial_count_;
}

std::uint64_t LocalApicTimer::remaining_count() const noexcept
{
    return this->remaining_count_;
}

std::uint64_t LocalApicTimer::elapsed_ticks() const noexcept
{
    return this->elapsed_ticks_;
}

void LocalApicTimer::configure(
    const InterruptVector vector,
    const std::uint64_t interval_ticks,
    const ApicTimerMode mode)
{
    if (interval_ticks < APIC_TIMER_MIN_INTERVAL_TICKS)
    {
        throw std::invalid_argument{APIC_TIMER_ZERO_INTERVAL_MESSAGE};
    }

    this->vector_ = vector;
    this->initial_count_ = interval_ticks;
    this->remaining_count_ = interval_ticks;
    this->mode_ = mode;
    this->enabled_ = true;
}

void LocalApicTimer::disable() noexcept
{
    this->enabled_ = false;
}

std::optional<InterruptVector> LocalApicTimer::tick() noexcept
{
    if (!this->enabled_)
    {
        return std::nullopt;
    }

    ++this->elapsed_ticks_;
    --this->remaining_count_;
    if (this->remaining_count_ != std::uint64_t{0})
    {
        return std::nullopt;
    }

    const InterruptVector fired_vector = this->vector_;
    if (this->mode_ == ApicTimerMode::PERIODIC)
    {
        this->remaining_count_ = this->initial_count_;
    }
    else
    {
        this->enabled_ = false;
    }
    return fired_vector;
}

LocalApic::LocalApic(const CoreId core_id) noexcept : core_id_(core_id)
{
}

CoreId LocalApic::core_id() const noexcept
{
    return this->core_id_;
}

bool LocalApic::enabled() const noexcept
{
    return this->enabled_;
}

void LocalApic::enable() noexcept
{
    this->enabled_ = true;
}

void LocalApic::disable() noexcept
{
    this->enabled_ = false;
}

LocalApicTimer& LocalApic::timer() noexcept
{
    return this->timer_;
}

const LocalApicTimer& LocalApic::timer() const noexcept
{
    return this->timer_;
}

void LocalApic::configure_periodic_timer(const InterruptVector vector, const std::uint64_t interval_ticks)
{
    this->timer_.configure(vector, interval_ticks, ApicTimerMode::PERIODIC);
}

void LocalApic::configure_one_shot_timer(const InterruptVector vector, const std::uint64_t interval_ticks)
{
    this->timer_.configure(vector, interval_ticks, ApicTimerMode::ONE_SHOT);
}

std::size_t LocalApic::pending_interrupt_count() const noexcept
{
    return this->pending_interrupts_.size();
}

std::optional<ApicInterrupt> LocalApic::tick() noexcept
{
    if (!this->enabled_)
    {
        return std::nullopt;
    }

    const std::optional<InterruptVector> fired_vector = this->timer_.tick();
    if (!fired_vector.has_value())
    {
        return std::nullopt;
    }

    this->pending_interrupts_.push_back(ApicInterrupt::timer(this->core_id_, fired_vector.value()));
    return this->pending_interrupts_.back();
}

bool LocalApic::receive_interrupt(const ApicInterrupt& interrupt)
{
    if (interrupt.target_core() != this->core_id_)
    {
        throw std::logic_error{LOCAL_APIC_TARGET_MISMATCH_MESSAGE};
    }
    if (!this->enabled_)
    {
        return false;
    }

    this->pending_interrupts_.push_back(interrupt);
    return true;
}

std::optional<ApicInterrupt> LocalApic::take_pending_interrupt()
{
    if (!this->enabled_ || this->pending_interrupts_.empty())
    {
        return std::nullopt;
    }

    const ApicInterrupt interrupt = this->pending_interrupts_.front();
    this->pending_interrupts_.pop_front();
    return interrupt;
}

IoApicRoute::IoApicRoute(
    const InterruptVector vector,
    const CoreId target_core,
    const bool masked) noexcept :
    vector_(vector),
    target_core_(target_core), masked_(masked)
{
}

InterruptVector IoApicRoute::vector() const noexcept
{
    return this->vector_;
}

CoreId IoApicRoute::target_core() const noexcept
{
    return this->target_core_;
}

bool IoApicRoute::masked() const noexcept
{
    return this->masked_;
}

IoApicRoute IoApicRoute::with_masked(const bool masked) const noexcept
{
    return IoApicRoute{this->vector_, this->target_core_, masked};
}

bool IoApic::has_route(const std::uint8_t irq_line) const
{
    return this->routes_[IoApic::irq_index(irq_line)].present;
}

const IoApicRoute& IoApic::route_at(const std::uint8_t irq_line) const
{
    const RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    if (!slot.present)
    {
        throw std::out_of_range{IO_APIC_ROUTE_NOT_PRESENT_MESSAGE};
    }
    return slot.route;
}

void IoApic::set_route(
    const std::uint8_t irq_line,
    const InterruptVector vector,
    const CoreId target_core)
{
    RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    slot.route = IoApicRoute{vector, target_core, false};
    slot.present = true;
}

void IoApic::clear_route(const std::uint8_t irq_line)
{
    RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    slot.present = false;
}

void IoApic::mask_irq(const std::uint8_t irq_line)
{
    RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    if (!slot.present)
    {
        throw std::out_of_range{IO_APIC_ROUTE_NOT_PRESENT_MESSAGE};
    }
    slot.route = slot.route.with_masked(true);
}

void IoApic::unmask_irq(const std::uint8_t irq_line)
{
    RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    if (!slot.present)
    {
        throw std::out_of_range{IO_APIC_ROUTE_NOT_PRESENT_MESSAGE};
    }
    slot.route = slot.route.with_masked(false);
}

std::optional<ApicInterrupt> IoApic::raise_irq(const std::uint8_t irq_line) const
{
    const RouteSlot& slot = this->routes_[IoApic::irq_index(irq_line)];
    if (!slot.present || slot.route.masked())
    {
        return std::nullopt;
    }
    return ApicInterrupt::external_irq(irq_line, slot.route.target_core(), slot.route.vector());
}

std::size_t IoApic::irq_index(const std::uint8_t irq_line)
{
    const std::size_t index = static_cast<std::size_t>(irq_line);
    if (index >= IO_APIC_IRQ_LINE_COUNT)
    {
        throw std::out_of_range{IO_APIC_IRQ_OUT_OF_RANGE_MESSAGE};
    }
    return index;
}

ApicSystem::ApicSystem(CoreTopology topology) : topology_(topology)
{
    this->local_apics_.reserve(this->topology_.core_count());
    for (std::uint32_t core_index = std::uint32_t{0}; core_index < this->topology_.core_count(); ++core_index)
    {
        this->local_apics_.emplace_back(this->topology_.core_at(core_index));
    }
}

const CoreTopology& ApicSystem::topology() const noexcept
{
    return this->topology_;
}

std::size_t ApicSystem::local_apic_count() const noexcept
{
    return this->local_apics_.size();
}

LocalApic& ApicSystem::local_apic(const CoreId core_id)
{
    return this->local_apics_[this->local_apic_index(core_id)];
}

const LocalApic& ApicSystem::local_apic(const CoreId core_id) const
{
    return this->local_apics_[this->local_apic_index(core_id)];
}

IoApic& ApicSystem::io_apic() noexcept
{
    return this->io_apic_;
}

const IoApic& ApicSystem::io_apic() const noexcept
{
    return this->io_apic_;
}

void ApicSystem::enable_local_apics() noexcept
{
    for (LocalApic& local_apic : this->local_apics_)
    {
        local_apic.enable();
    }
}

std::optional<ApicInterrupt> ApicSystem::tick(const CoreId core_id)
{
    return this->local_apic(core_id).tick();
}

bool ApicSystem::send_ipi(
    const CoreId source_core,
    const CoreId target_core,
    const InterruptVector vector)
{
    static_cast<void>(this->local_apic_index(source_core));
    return this->local_apic(target_core).receive_interrupt(ApicInterrupt::ipi(source_core, target_core, vector));
}

std::size_t ApicSystem::broadcast_ipi(const CoreId source_core, const InterruptVector vector)
{
    static_cast<void>(this->local_apic_index(source_core));
    std::size_t delivered_count = std::size_t{0};
    for (LocalApic& local_apic : this->local_apics_)
    {
        if (local_apic.core_id() == source_core)
        {
            continue;
        }
        if (local_apic.receive_interrupt(ApicInterrupt::ipi(source_core, local_apic.core_id(), vector)))
        {
            ++delivered_count;
        }
    }
    return delivered_count;
}

std::optional<ApicInterrupt> ApicSystem::raise_irq(const std::uint8_t irq_line)
{
    const std::optional<ApicInterrupt> interrupt = this->io_apic_.raise_irq(irq_line);
    if (!interrupt.has_value())
    {
        return std::nullopt;
    }
    if (!this->local_apic(interrupt->target_core()).receive_interrupt(interrupt.value()))
    {
        return std::nullopt;
    }
    return interrupt;
}

std::optional<ApicInterrupt> ApicSystem::take_pending_interrupt(const CoreId core_id)
{
    return this->local_apic(core_id).take_pending_interrupt();
}

std::size_t ApicSystem::local_apic_index(const CoreId core_id) const
{
    if (!this->topology_.contains(core_id))
    {
        throw std::out_of_range{APIC_CORE_OUT_OF_RANGE_MESSAGE};
    }
    return static_cast<std::size_t>(core_id.value());
}
}
