#include <new>
#include <stdexcept>

#include <mnos/cpu/register/id.hpp>
#include <mnos/os/kernel/kernel.hpp>

namespace
{
constexpr const char* KERNEL_ALREADY_BOOTED_MESSAGE = "kernel has already booted";
constexpr const char* KERNEL_INSUFFICIENT_MEMORY_MESSAGE = "kernel requires enough physical pages for stage5 services";
constexpr const char* KERNEL_NOT_BOOTED_MESSAGE = "kernel has not booted";
constexpr const char* KERNEL_STAGE5_NOT_READY_MESSAGE = "kernel stage5 services are not initialized";
constexpr const char* KERNEL_PROCESS_INDEX_OUT_OF_RANGE_MESSAGE = "kernel process index is out of range";
}

namespace mnos::os::kernel
{
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
}
