#include <new>
#include <limits>
#include <stdexcept>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/kernel.hpp>

namespace
{
constexpr const char* KERNEL_ALREADY_BOOTED_MESSAGE = "kernel has already booted";
constexpr const char* KERNEL_INSUFFICIENT_MEMORY_MESSAGE = "kernel requires enough physical pages for stage5 services";
constexpr const char* KERNEL_NOT_BOOTED_MESSAGE = "kernel has not booted";
constexpr const char* KERNEL_STAGE5_NOT_READY_MESSAGE = "kernel stage5 services are not initialized";
constexpr const char* KERNEL_STAGE7_NOT_READY_MESSAGE = "kernel stage7 services are not initialized";
constexpr const char* KERNEL_PROCESS_INDEX_OUT_OF_RANGE_MESSAGE = "kernel process index is out of range";
constexpr const char* KERNEL_SCHEDULER_HANDOFF_INDEX_OUT_OF_RANGE_MESSAGE =
    "kernel scheduler handoff index is out of range";
constexpr const char* KERNEL_SCHEDULER_HANDOFF_DEAD_THREAD_MESSAGE =
    "kernel cannot hand off a dead thread";
constexpr const char* KERNEL_SLEEP_TICK_OVERFLOW_MESSAGE = "kernel sleep duration overflows scheduler tick";
}

namespace mnos::os::kernel
{
SchedulerHandoff::SchedulerHandoff(
    const std::uint64_t sequence,
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    const sched::ThreadId thread_id) noexcept :
    sequence_(sequence),
    source_core_(source_core), target_core_(target_core), thread_id_(thread_id)
{
}

std::uint64_t SchedulerHandoff::sequence() const noexcept
{
    return this->sequence_;
}

cpu::system::CoreId SchedulerHandoff::source_core() const noexcept
{
    return this->source_core_;
}

cpu::system::CoreId SchedulerHandoff::target_core() const noexcept
{
    return this->target_core_;
}

sched::ThreadId SchedulerHandoff::thread_id() const noexcept
{
    return this->thread_id_;
}

Kernel::Kernel(BootContext& boot_context) noexcept : boot_context_(&boot_context)
{
}

void Kernel::boot()
{
    if (this->booted_)
    {
        throw std::logic_error{KERNEL_ALREADY_BOOTED_MESSAGE};
    }

    if (this->boot_context_->physical_page_count() < KERNEL_MIN_BOOTABLE_PAGE_COUNT)
    {
        throw std::runtime_error{KERNEL_INSUFFICIENT_MEMORY_MESSAGE};
    }

    this->physical_page_allocator_.emplace(
        this->boot_context_->physical_page_count(),
        KERNEL_RESERVED_LOW_PAGE_COUNT);
    this->kernel_address_space_.emplace(this->create_address_space(KERNEL_DEFAULT_TABLE_ARENA_PAGE_COUNT));
    this->configure_stage7_services();
    this->booted_ = true;
}

bool Kernel::is_booted() const noexcept
{
    return this->booted_;
}

BootContext& Kernel::boot_context() noexcept
{
    return *this->boot_context_;
}

const BootContext& Kernel::boot_context() const noexcept
{
    return *this->boot_context_;
}

std::size_t Kernel::physical_memory_size_bytes() const noexcept
{
    return this->boot_context_->physical_memory_size_bytes();
}

std::uint64_t Kernel::physical_page_count() const noexcept
{
    return this->boot_context_->physical_page_count();
}

std::uint32_t Kernel::bootstrap_processor_count() const noexcept
{
    return this->boot_context_->bootstrap_processor_count();
}

bool Kernel::has_stage5_services() const noexcept
{
    return this->physical_page_allocator_.has_value() && this->kernel_address_space_.has_value();
}

bool Kernel::has_stage7_services() const noexcept
{
    return this->has_stage5_services() && this->apic_system_.has_value();
}

mm::PhysicalPageAllocator& Kernel::physical_page_allocator()
{
    this->require_stage5_services();
    return this->physical_page_allocator_.value();
}

const mm::PhysicalPageAllocator& Kernel::physical_page_allocator() const
{
    this->require_stage5_services();
    return this->physical_page_allocator_.value();
}

mm::AddressSpace& Kernel::kernel_address_space()
{
    this->require_stage5_services();
    return this->kernel_address_space_.value();
}

const mm::AddressSpace& Kernel::kernel_address_space() const
{
    this->require_stage5_services();
    return this->kernel_address_space_.value();
}

sched::RoundRobinScheduler& Kernel::scheduler() noexcept
{
    return this->scheduler_;
}

const sched::RoundRobinScheduler& Kernel::scheduler() const noexcept
{
    return this->scheduler_;
}

cpu::system::ApicSystem& Kernel::apic_system()
{
    this->require_stage7_services();
    return this->apic_system_.value();
}

const cpu::system::ApicSystem& Kernel::apic_system() const
{
    this->require_stage7_services();
    return this->apic_system_.value();
}

sched::SleepQueue& Kernel::sleep_queue() noexcept
{
    return this->sleep_queue_;
}

const sched::SleepQueue& Kernel::sleep_queue() const noexcept
{
    return this->sleep_queue_;
}

cpu::memory::TlbShootdownController& Kernel::tlb_shootdown_controller() noexcept
{
    return this->tlb_shootdown_controller_;
}

const cpu::memory::TlbShootdownController& Kernel::tlb_shootdown_controller() const noexcept
{
    return this->tlb_shootdown_controller_;
}

proc::Process& Kernel::create_process()
{
    this->require_stage5_services();
    const proc::ProcessId process_id{this->next_process_id_value_};
    ++this->next_process_id_value_;
    this->processes_.emplace_back(process_id, this->create_address_space(KERNEL_DEFAULT_TABLE_ARENA_PAGE_COUNT));
    return this->processes_.back();
}

sched::ThreadContext& Kernel::create_thread(proc::Process& process)
{
    return this->create_thread(
        process,
        this->next_kernel_stack_bottom(),
        sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);
}

sched::ThreadContext& Kernel::create_thread(
    proc::Process& process,
    const mm::VirtualAddress kernel_stack_bottom,
    const std::uint64_t kernel_stack_size_bytes)
{
    this->require_stage5_services();
    const sched::ThreadId thread_id{this->next_thread_id_value_};
    ++this->next_thread_id_value_;
    sched::ThreadContext& thread = process.create_thread(thread_id, kernel_stack_bottom, kernel_stack_size_bytes);
    this->scheduler_.enqueue(thread);
    return thread;
}

std::size_t Kernel::process_count() const noexcept
{
    return this->processes_.size();
}

proc::Process& Kernel::process_at(const std::size_t index)
{
    if (index >= this->processes_.size())
    {
        throw std::out_of_range{KERNEL_PROCESS_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->processes_[index];
}

const proc::Process& Kernel::process_at(const std::size_t index) const
{
    if (index >= this->processes_.size())
    {
        throw std::out_of_range{KERNEL_PROCESS_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->processes_[index];
}

mm::PageFaultResult Kernel::handle_page_fault(sched::ThreadContext& thread)
{
    this->require_stage5_services();
    mm::PageFaultHandler handler{
        this->physical_page_allocator_.value(),
        this->kernel_address_space_.value(),
        this->boot_context_->memory_bus()};
    return handler.handle(thread.cpu_state().pending_trap(), thread.cpu_state());
}

mm::PageFaultResult Kernel::handle_page_fault(proc::Process& process, sched::ThreadContext& thread)
{
    this->require_stage5_services();
    mm::PageFaultHandler handler{
        this->physical_page_allocator_.value(),
        process.address_space(),
        this->boot_context_->memory_bus()};
    return handler.handle(thread.cpu_state().pending_trap(), thread.cpu_state());
}

SyscallResult Kernel::dispatch_syscall(sched::ThreadContext& thread)
{
    this->require_booted();
    const SyscallNumber syscall_number =
        syscall_number_from_raw(thread.cpu_state().registers().read(cpu::RegisterId::RAX));
    if (!is_syscall_number_valid(syscall_number))
    {
        thread.cpu_state().registers().write(cpu::RegisterId::RAX, SYSCALL_UNSUPPORTED_RESULT);
        return SyscallResult::UNSUPPORTED;
    }

    switch (syscall_number)
    {
    case SyscallNumber::YIELD:
        if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
        {
            static_cast<void>(this->scheduler_.yield_current());
        }
        thread.cpu_state().registers().write(cpu::RegisterId::RAX, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    case SyscallNumber::EXIT:
        if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
        {
            static_cast<void>(this->scheduler_.exit_current());
        }
        else
        {
            thread.set_state(sched::ThreadState::DEAD);
        }
        thread.cpu_state().registers().write(cpu::RegisterId::RAX, SYSCALL_SUCCESS_RESULT);
        return SyscallResult::HANDLED;
    case SyscallNumber::COUNT:
        break;
    }

    thread.cpu_state().registers().write(cpu::RegisterId::RAX, SYSCALL_UNSUPPORTED_RESULT);
    return SyscallResult::UNSUPPORTED;
}

sched::SchedulerTick Kernel::scheduler_tick_count() const noexcept
{
    return this->scheduler_tick_count_;
}

std::optional<cpu::system::ApicInterrupt> Kernel::tick_core_timer(const cpu::system::CoreId core_id)
{
    this->require_stage7_services();
    const std::optional<cpu::system::ApicInterrupt> interrupt = this->apic_system_.value().tick(core_id);
    if (interrupt.has_value() && interrupt->vector() == cpu::system::InterruptVector::timer())
    {
        static_cast<void>(this->handle_timer_interrupt(core_id));
    }
    return interrupt;
}

sched::ThreadContext* Kernel::handle_timer_interrupt(const cpu::system::CoreId core_id)
{
    this->require_stage7_services();
    static_cast<void>(this->apic_system_.value().local_apic(core_id));
    ++this->scheduler_tick_count_;
    static_cast<void>(this->wake_sleepers());
    if (this->scheduler_.has_current())
    {
        return this->scheduler_.yield_current();
    }
    return this->scheduler_.schedule_next();
}

sched::ThreadContext* Kernel::sleep_current_until(const sched::SchedulerTick wake_tick)
{
    this->require_stage7_services();
    if (!this->scheduler_.has_current())
    {
        return nullptr;
    }
    if (wake_tick <= this->scheduler_tick_count_)
    {
        return this->scheduler_.yield_current();
    }

    this->sleep_queue_.sleep_until(this->scheduler_.current(), wake_tick);
    return this->scheduler_.block_current();
}

sched::ThreadContext* Kernel::sleep_current_for(const sched::SchedulerTick duration_ticks)
{
    this->require_stage7_services();
    if (duration_ticks > std::numeric_limits<sched::SchedulerTick>::max() - this->scheduler_tick_count_)
    {
        throw std::overflow_error{KERNEL_SLEEP_TICK_OVERFLOW_MESSAGE};
    }
    return this->sleep_current_until(this->scheduler_tick_count_ + duration_ticks);
}

std::size_t Kernel::wake_sleepers()
{
    this->require_stage7_services();
    const std::vector<sched::ThreadContext*> ready_threads = this->sleep_queue_.take_ready(this->scheduler_tick_count_);
    for (sched::ThreadContext* const thread : ready_threads)
    {
        this->scheduler_.wake(*thread);
    }
    return ready_threads.size();
}

const cpu::memory::TlbShootdownRequest& Kernel::request_tlb_shootdown_page(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    const cpu::Address64 linear_address,
    std::optional<cpu::memory::ProcessContextId> context_id)
{
    this->require_stage7_services();
    this->validate_ipi_route(source_core, target_core);
    const cpu::memory::TlbShootdownRequest& request =
        this->tlb_shootdown_controller_.request_page(source_core, target_core, linear_address, context_id);
    static_cast<void>(
        this->apic_system_.value().send_ipi(source_core, target_core, cpu::system::InterruptVector::tlb_shootdown()));
    return request;
}

const cpu::memory::TlbShootdownRequest& Kernel::request_tlb_shootdown_all(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    std::optional<cpu::memory::ProcessContextId> context_id)
{
    this->require_stage7_services();
    this->validate_ipi_route(source_core, target_core);
    const cpu::memory::TlbShootdownRequest& request =
        this->tlb_shootdown_controller_.request_all(source_core, target_core, context_id);
    static_cast<void>(
        this->apic_system_.value().send_ipi(source_core, target_core, cpu::system::InterruptVector::tlb_shootdown()));
    return request;
}

const SchedulerHandoff& Kernel::request_scheduler_handoff(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    sched::ThreadContext& thread)
{
    this->require_stage7_services();
    if (!thread.is_alive())
    {
        throw std::logic_error{KERNEL_SCHEDULER_HANDOFF_DEAD_THREAD_MESSAGE};
    }
    this->validate_ipi_route(source_core, target_core);

    thread.cpu_state().set_core_id(target_core);
    this->scheduler_handoffs_.emplace_back(
        this->next_scheduler_handoff_sequence(),
        source_core,
        target_core,
        thread.id());
    static_cast<void>(
        this->apic_system_.value().send_ipi(source_core, target_core, cpu::system::InterruptVector::reschedule()));
    return this->scheduler_handoffs_.back();
}

std::size_t Kernel::scheduler_handoff_count() const noexcept
{
    return this->scheduler_handoffs_.size();
}

const SchedulerHandoff& Kernel::scheduler_handoff_at(const std::size_t index) const
{
    if (index >= this->scheduler_handoffs_.size())
    {
        throw std::out_of_range{KERNEL_SCHEDULER_HANDOFF_INDEX_OUT_OF_RANGE_MESSAGE};
    }
    return this->scheduler_handoffs_[index];
}

mm::AddressSpace Kernel::create_address_space(const std::uint64_t table_arena_page_count)
{
    const std::uint64_t free_page_count = this->physical_page_allocator_.value().free_page_count();
    const std::uint64_t actual_table_arena_page_count =
        table_arena_page_count < free_page_count ? table_arena_page_count : free_page_count;
    if (actual_table_arena_page_count == std::uint64_t{0})
    {
        throw std::bad_alloc{};
    }

    const mm::PhysicalAddress table_arena_start =
        this->physical_page_allocator_.value().allocate_contiguous(actual_table_arena_page_count);
    const mm::PhysicalAddress next_free_table_address = table_arena_start + mm::MM_PAGE_SIZE_BYTES;
    const mm::PhysicalAddress table_arena_end_address =
        table_arena_start + (actual_table_arena_page_count * mm::MM_PAGE_SIZE_BYTES);
    return mm::AddressSpace{
        this->boot_context_->memory_bus(),
        table_arena_start,
        next_free_table_address,
        table_arena_end_address};
}

mm::VirtualAddress Kernel::next_kernel_stack_bottom() noexcept
{
    const mm::VirtualAddress stack_bottom{this->next_kernel_stack_bottom_value_};
    this->next_kernel_stack_bottom_value_ += KERNEL_THREAD_STACK_STRIDE;
    return stack_bottom;
}

void Kernel::require_booted() const
{
    if (!this->booted_)
    {
        throw std::logic_error{KERNEL_NOT_BOOTED_MESSAGE};
    }
}

void Kernel::require_stage5_services() const
{
    this->require_booted();
    if (!this->has_stage5_services())
    {
        throw std::logic_error{KERNEL_STAGE5_NOT_READY_MESSAGE};
    }
}

void Kernel::require_stage7_services() const
{
    this->require_booted();
    if (!this->has_stage7_services())
    {
        throw std::logic_error{KERNEL_STAGE7_NOT_READY_MESSAGE};
    }
}

void Kernel::configure_stage7_services()
{
    this->apic_system_.emplace(this->boot_context_->machine().core_topology());
    this->apic_system_.value().enable_local_apics();
    for (std::uint32_t core_index = std::uint32_t{0};
         core_index < this->apic_system_.value().topology().core_count();
         ++core_index)
    {
        this->apic_system_.value().local_apic(cpu::system::CoreId{core_index}).configure_periodic_timer(
            cpu::system::InterruptVector::timer(),
            KERNEL_STAGE7_DEFAULT_TIMER_INTERVAL_TICKS);
    }
}

void Kernel::validate_ipi_route(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core) const
{
    static_cast<void>(this->apic_system_.value().local_apic(source_core));
    static_cast<void>(this->apic_system_.value().local_apic(target_core));
}

std::uint64_t Kernel::next_scheduler_handoff_sequence() noexcept
{
    const std::uint64_t sequence = this->next_scheduler_handoff_sequence_;
    ++this->next_scheduler_handoff_sequence_;
    return sequence;
}
}
