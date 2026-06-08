#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <mnos/os/kernel/kernel.hpp>

namespace
{
constexpr const char* KERNEL_ALREADY_BOOTED_MESSAGE = "kernel has already booted";
constexpr const char* KERNEL_INSUFFICIENT_MEMORY_MESSAGE = "kernel requires enough physical pages for stage5 services";
constexpr const char* KERNEL_NOT_BOOTED_MESSAGE = "kernel has not booted";
constexpr const char* KERNEL_STAGE5_NOT_READY_MESSAGE = "kernel stage5 services are not initialized";
constexpr const char* KERNEL_STAGE7_NOT_READY_MESSAGE = "kernel stage7 services are not initialized";
constexpr const char* KERNEL_STAGE9_NOT_READY_MESSAGE = "kernel stage9 services are not initialized";
constexpr const char* KERNEL_STAGE10_NOT_READY_MESSAGE = "kernel stage10 services are not initialized";
constexpr const char* KERNEL_STAGE12_NOT_READY_MESSAGE = "kernel stage12 services are not initialized";
constexpr const char* KERNEL_STAGE13_NOT_READY_MESSAGE = "kernel stage13 services are not initialized";
constexpr const char* KERNEL_STAGE15_NOT_READY_MESSAGE = "kernel stage15 services are not initialized";
constexpr const char* KERNEL_PROCESS_INDEX_OUT_OF_RANGE_MESSAGE = "kernel process index is out of range";
constexpr const char* KERNEL_SCHEDULER_HANDOFF_INDEX_OUT_OF_RANGE_MESSAGE =
    "kernel scheduler handoff index is out of range";
constexpr const char* KERNEL_SCHEDULER_HANDOFF_DEAD_THREAD_MESSAGE =
    "kernel cannot hand off a dead thread";
constexpr const char* KERNEL_SMP_MIGRATION_SOURCE_MISMATCH_MESSAGE =
    "kernel smp migration source core does not own the ready thread";
constexpr const char* KERNEL_SLEEP_TICK_OVERFLOW_MESSAGE = "kernel sleep duration overflows scheduler tick";
constexpr const char* KERNEL_INVALID_FILE_OPEN_MODE_MESSAGE = "kernel file open mode is invalid";

[[nodiscard]] mnos::os::fs::VfsOpenMode kernel_vfs_mode_from_access_mode(
    const mnos::os::io::FileAccessMode access_mode) noexcept
{
    switch (access_mode)
    {
    case mnos::os::io::FileAccessMode::READ_ONLY:
        return mnos::os::fs::VfsOpenMode::READ_ONLY;
    case mnos::os::io::FileAccessMode::WRITE_ONLY:
        return mnos::os::fs::VfsOpenMode::WRITE_ONLY;
    case mnos::os::io::FileAccessMode::READ_WRITE:
        return mnos::os::fs::VfsOpenMode::READ_WRITE;
    case mnos::os::io::FileAccessMode::COUNT:
        break;
    }
    return mnos::os::fs::VfsOpenMode::COUNT;
}
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

Kernel::Kernel(BootContext& boot_context) noexcept :
    boot_context_(&boot_context),
    console_(boot_context.terminal_device())
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
    this->configure_stage9_services();
    this->configure_stage15_services();
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

bool Kernel::has_stage9_services() const noexcept
{
    return this->has_stage7_services() && this->smp_scheduler_.has_value();
}

bool Kernel::has_stage10_services() const noexcept
{
    return this->has_stage9_services();
}

bool Kernel::has_stage11_services() const noexcept
{
    return this->has_stage10_services();
}

bool Kernel::has_stage12_services() const noexcept
{
    return this->has_stage11_services();
}

bool Kernel::has_stage13_services() const noexcept
{
    return this->has_stage12_services();
}

bool Kernel::has_stage15_services() const noexcept
{
    return this->has_stage13_services() && this->root_block_device_.has_value() &&
           this->root_buffer_cache_.has_value() && this->root_file_system_.has_value() &&
           this->vfs_.has_value();
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

sched::SmpScheduler& Kernel::smp_scheduler()
{
    this->require_stage9_services();
    return this->smp_scheduler_.value();
}

const sched::SmpScheduler& Kernel::smp_scheduler() const
{
    this->require_stage9_services();
    return this->smp_scheduler_.value();
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

proc::CopyOnWriteManager& Kernel::copy_on_write_manager() noexcept
{
    return this->copy_on_write_manager_;
}

const proc::CopyOnWriteManager& Kernel::copy_on_write_manager() const noexcept
{
    return this->copy_on_write_manager_;
}

proc::FutexTable& Kernel::futex_table() noexcept
{
    return this->futex_table_;
}

const proc::FutexTable& Kernel::futex_table() const noexcept
{
    return this->futex_table_;
}

tty::Console& Kernel::console() noexcept
{
    return this->console_;
}

const tty::Console& Kernel::console() const noexcept
{
    return this->console_;
}

fs::Vfs& Kernel::vfs()
{
    this->require_stage15_services();
    return this->vfs_.value();
}

const fs::Vfs& Kernel::vfs() const
{
    this->require_stage15_services();
    return this->vfs_.value();
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

sched::ThreadContext& Kernel::create_thread_on_core(
    proc::Process& process,
    const cpu::system::CoreId target_core)
{
    return this->create_thread_on_core(
        process,
        target_core,
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

sched::ThreadContext& Kernel::create_thread_on_core(
    proc::Process& process,
    const cpu::system::CoreId target_core,
    const mm::VirtualAddress kernel_stack_bottom,
    const std::uint64_t kernel_stack_size_bytes)
{
    this->require_stage9_services();
    static_cast<void>(this->smp_scheduler_.value().core_load(target_core));
    const sched::ThreadId thread_id{this->next_thread_id_value_};
    ++this->next_thread_id_value_;
    sched::ThreadContext& thread = process.create_thread(thread_id, kernel_stack_bottom, kernel_stack_size_bytes);
    this->smp_scheduler_.value().enqueue(thread, target_core);
    return thread;
}

proc::UserProcessImage Kernel::load_user_program(
    proc::Process& process,
    const proc::UserProgram& program)
{
    this->require_stage10_services();
    proc::UserLoader loader{this->physical_page_allocator_.value(), this->boot_context_->memory_bus()};
    return loader.load(program, process);
}

sched::ThreadContext& Kernel::create_user_thread(
    proc::Process& process,
    const proc::UserProcessImage& image)
{
    this->require_stage10_services();
    const sched::ThreadId thread_id{this->next_thread_id_value_};
    ++this->next_thread_id_value_;
    sched::ThreadContext& thread = process.create_thread(
        thread_id,
        this->next_kernel_stack_bottom(),
        sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);
    proc::UserLoader loader{this->physical_page_allocator_.value(), this->boot_context_->memory_bus()};
    loader.initialize_user_thread(image, process, thread);
    this->scheduler_.enqueue(thread);
    return thread;
}

proc::Process& Kernel::create_user_process(const proc::UserProgram& program)
{
    proc::Process& process = this->create_process();
    const proc::UserProcessImage image = this->load_user_program(process, program);
    static_cast<void>(this->create_user_thread(process, image));
    return process;
}

proc::Process& Kernel::fork_process_cow(
    proc::Process& parent_process,
    std::span<const mm::VirtualAddress> cow_pages)
{
    this->require_stage10_services();
    proc::Process& child_process = this->create_process();
    child_process.file_descriptors() = parent_process.file_descriptors();
    static_cast<void>(this->copy_on_write_manager_.share_pages(parent_process, child_process, cow_pages));
    return child_process;
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

void Kernel::console_write(const std::string_view text)
{
    this->require_stage12_services();
    this->console_.write(text);
}

tty::ConsoleReadResult Kernel::console_read(
    sched::ThreadContext& thread,
    std::span<char> destination)
{
    this->require_stage12_services();
    const tty::ConsoleReadResult result = this->console_.read(destination, thread);
    if (result.is_blocked() && this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
    {
        static_cast<void>(this->scheduler_.block_current());
    }
    return result;
}

std::vector<sched::ThreadContext*> Kernel::submit_terminal_input(const std::string_view text)
{
    this->require_stage12_services();
    std::vector<sched::ThreadContext*> readers = this->console_.submit_input(text);
    for (sched::ThreadContext* const reader : readers)
    {
        this->scheduler_.wake(*reader);
    }
    return readers;
}

io::IoResult Kernel::read_fd(
    proc::Process& process,
    sched::ThreadContext& thread,
    const io::FileDescriptor descriptor,
    std::span<char> destination)
{
    this->require_stage13_services();
    io::FileDescriptorEntry* const entry = process.file_descriptors().find_mutable(descriptor);
    if (entry == nullptr || !entry->readable())
    {
        return io::IoResult::bad_descriptor();
    }

    switch (entry->device_kind())
    {
    case io::FileDeviceKind::TTY:
    {
        const tty::ConsoleReadResult result = this->console_read(thread, destination);
        return result.is_blocked() ? io::IoResult::blocked() : io::IoResult::ready(result.byte_count());
    }
    case io::FileDeviceKind::VFS_FILE:
    {
        try
        {
            std::vector<cpu::Byte> bytes(destination.size());
            const std::size_t byte_count = entry->description().vfs_file().read(bytes);
            for (std::size_t byte_index = std::size_t{0}; byte_index < byte_count; ++byte_index)
            {
                destination[byte_index] = static_cast<char>(bytes[byte_index]);
            }
            return io::IoResult::ready(byte_count);
        }
        catch (const std::invalid_argument&)
        {
            return io::IoResult::invalid_argument();
        }
        catch (const std::out_of_range&)
        {
            return io::IoResult::bad_descriptor();
        }
        catch (const std::logic_error&)
        {
            return io::IoResult::bad_descriptor();
        }
    }
    case io::FileDeviceKind::COUNT:
    default:
        return io::IoResult::bad_descriptor();
    }
}

io::IoResult Kernel::write_fd(
    proc::Process& process,
    const io::FileDescriptor descriptor,
    const std::string_view text)
{
    this->require_stage13_services();
    io::FileDescriptorEntry* const entry = process.file_descriptors().find_mutable(descriptor);
    if (entry == nullptr || !entry->writable())
    {
        return io::IoResult::bad_descriptor();
    }

    switch (entry->device_kind())
    {
    case io::FileDeviceKind::TTY:
        this->console_write(text);
        return io::IoResult::ready(text.size());
    case io::FileDeviceKind::VFS_FILE:
    {
        try
        {
            std::vector<cpu::Byte> bytes;
            bytes.reserve(text.size());
            for (const char character : text)
            {
                bytes.push_back(static_cast<cpu::Byte>(static_cast<unsigned char>(character)));
            }
            return io::IoResult::ready(entry->description().vfs_file().write(bytes));
        }
        catch (const std::length_error&)
        {
            return io::IoResult::no_space();
        }
        catch (const std::invalid_argument&)
        {
            return io::IoResult::invalid_argument();
        }
        catch (const std::out_of_range&)
        {
            return io::IoResult::bad_descriptor();
        }
        catch (const std::logic_error&)
        {
            return io::IoResult::bad_descriptor();
        }
    }
    case io::FileDeviceKind::COUNT:
    default:
        return io::IoResult::bad_descriptor();
    }
}

io::FileDescriptor Kernel::open_file(
    proc::Process& process,
    const std::string_view path,
    const io::FileAccessMode access_mode,
    const bool create_if_missing)
{
    this->require_stage15_services();
    const fs::VfsOpenMode mode = kernel_vfs_mode_from_access_mode(access_mode);
    if (mode == fs::VfsOpenMode::COUNT)
    {
        throw std::invalid_argument{KERNEL_INVALID_FILE_OPEN_MODE_MESSAGE};
    }

    const std::optional<fs::VfsNode> node = this->vfs_.value().lookup(path);
    if (!node.has_value() && create_if_missing)
    {
        static_cast<void>(this->vfs_.value().create_file(path));
    }

    fs::VfsFile file = this->vfs_.value().open_file(path, mode);
    return process.file_descriptors().open_vfs_file(std::move(file));
}

bool Kernel::close_fd(proc::Process& process, const io::FileDescriptor descriptor)
{
    this->require_stage13_services();
    return process.file_descriptors().close(descriptor);
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
    if (this->smp_scheduler_.has_value() && this->smp_scheduler_.value().has_work_on_core(core_id))
    {
        return this->handle_smp_timer_interrupt(core_id);
    }

    ++this->scheduler_tick_count_;
    static_cast<void>(this->wake_sleepers());
    if (this->scheduler_.has_current())
    {
        return this->scheduler_.yield_current();
    }
    return this->scheduler_.schedule_next();
}

sched::ThreadContext* Kernel::handle_smp_timer_interrupt(const cpu::system::CoreId core_id)
{
    this->require_stage9_services();
    static_cast<void>(this->apic_system_.value().local_apic(core_id));
    ++this->scheduler_tick_count_;
    static_cast<void>(this->wake_sleepers());

    sched::SmpScheduler& scheduler = this->smp_scheduler_.value();
    const bool preempted = scheduler.has_current(core_id);
    scheduler.record_timer_tick(core_id, preempted);
    if (preempted)
    {
        return scheduler.yield_current(core_id);
    }
    return scheduler.schedule_next(core_id);
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

    const SchedulerHandoff& handoff = this->record_scheduler_handoff(source_core, target_core, thread);
    static_cast<void>(this->send_reschedule_ipi(source_core, target_core));
    return handoff;
}

bool Kernel::wake_thread_on_core(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    sched::ThreadContext& thread)
{
    this->require_stage9_services();
    this->validate_ipi_route(source_core, target_core);
    this->smp_scheduler_.value().wake(thread, target_core);
    return this->send_reschedule_ipi(source_core, target_core);
}

const SchedulerHandoff& Kernel::request_smp_migration(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    sched::ThreadContext& thread)
{
    this->require_stage9_services();
    this->validate_ipi_route(source_core, target_core);
    sched::SmpScheduler& scheduler = this->smp_scheduler_.value();
    const std::optional<cpu::system::CoreId> owning_core = scheduler.current_core_of(thread);
    if (owning_core.has_value() && owning_core.value() != source_core)
    {
        throw std::logic_error{KERNEL_SMP_MIGRATION_SOURCE_MISMATCH_MESSAGE};
    }

    const sched::ThreadMigration migration = scheduler.migrate_ready(thread, target_core);
    const SchedulerHandoff& handoff =
        this->record_scheduler_handoff(migration.source_core(), migration.target_core(), thread);
    static_cast<void>(this->send_reschedule_ipi(migration.source_core(), migration.target_core()));
    return handoff;
}

std::optional<sched::ThreadMigration> Kernel::rebalance_smp_once()
{
    this->require_stage9_services();
    std::optional<sched::ThreadMigration> migration = this->smp_scheduler_.value().rebalance_once();
    if (!migration.has_value())
    {
        return std::nullopt;
    }

    this->validate_ipi_route(migration->source_core(), migration->target_core());
    this->scheduler_handoffs_.emplace_back(
        this->next_scheduler_handoff_sequence(),
        migration->source_core(),
        migration->target_core(),
        migration->thread_id());
    static_cast<void>(this->send_reschedule_ipi(migration->source_core(), migration->target_core()));
    return migration;
}

bool Kernel::apply_next_tlb_shootdown_for_core(
    const cpu::system::CoreId core_id,
    cpu::memory::MemoryManagementUnit& mmu)
{
    this->require_stage9_services();
    static_cast<void>(this->apic_system_.value().local_apic(core_id));
    std::optional<cpu::memory::TlbShootdownRequest> request =
        this->tlb_shootdown_controller_.take_next_for(core_id);
    if (!request.has_value())
    {
        return false;
    }

    this->tlb_shootdown_controller_.apply(mmu, request.value());
    return true;
}

proc::CowFaultResult Kernel::handle_cow_write_fault(
    proc::Process& process,
    sched::ThreadContext& thread)
{
    this->require_stage10_services();
    return this->copy_on_write_manager_.resolve_write_fault(
        this->physical_page_allocator_.value(),
        this->boot_context_->memory_bus(),
        process,
        thread.cpu_state());
}

sched::ThreadContext* Kernel::wait_on_futex(
    proc::Process& process,
    const mm::VirtualAddress address,
    sched::ThreadContext& thread)
{
    this->require_stage10_services();
    this->futex_table_.wait(proc::FutexKey{process.id(), address}, thread);
    if (this->scheduler_.has_current() && &this->scheduler_.current() == &thread)
    {
        return this->scheduler_.block_current();
    }
    return nullptr;
}

sched::ThreadContext* Kernel::wake_one_futex(
    proc::Process& process,
    const mm::VirtualAddress address)
{
    this->require_stage10_services();
    sched::ThreadContext* const thread = this->futex_table_.wake_one(proc::FutexKey{process.id(), address});
    if (thread != nullptr)
    {
        this->scheduler_.wake(*thread);
    }
    return thread;
}

std::vector<sched::ThreadContext*> Kernel::wake_all_futex(
    proc::Process& process,
    const mm::VirtualAddress address)
{
    this->require_stage10_services();
    std::vector<sched::ThreadContext*> threads =
        this->futex_table_.wake_all(proc::FutexKey{process.id(), address});
    for (sched::ThreadContext* const thread : threads)
    {
        this->scheduler_.wake(*thread);
    }
    return threads;
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

void Kernel::require_stage9_services() const
{
    this->require_booted();
    if (!this->has_stage9_services())
    {
        throw std::logic_error{KERNEL_STAGE9_NOT_READY_MESSAGE};
    }
}

void Kernel::require_stage10_services() const
{
    this->require_booted();
    if (!this->has_stage10_services())
    {
        throw std::logic_error{KERNEL_STAGE10_NOT_READY_MESSAGE};
    }
}

void Kernel::require_stage12_services() const
{
    this->require_booted();
    if (!this->has_stage12_services())
    {
        throw std::logic_error{KERNEL_STAGE12_NOT_READY_MESSAGE};
    }
}

void Kernel::require_stage13_services() const
{
    this->require_booted();
    if (!this->has_stage13_services())
    {
        throw std::logic_error{KERNEL_STAGE13_NOT_READY_MESSAGE};
    }
}

void Kernel::require_stage15_services() const
{
    this->require_booted();
    if (!this->has_stage15_services())
    {
        throw std::logic_error{KERNEL_STAGE15_NOT_READY_MESSAGE};
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

void Kernel::configure_stage9_services()
{
    this->smp_scheduler_.emplace(this->boot_context_->machine().core_topology());
}

void Kernel::configure_stage15_services()
{
    this->root_block_device_.emplace(
        block::BlockDeviceGeometry{
            block::BLOCK_DEVICE_DEFAULT_BLOCK_SIZE_BYTES,
            KERNEL_STAGE15_ROOT_BLOCK_COUNT});
    this->root_buffer_cache_.emplace(
        this->root_block_device_.value(),
        KERNEL_STAGE15_BUFFER_CACHE_BLOCKS);
    fs::SimpleFileSystem::format(
        this->root_buffer_cache_.value(),
        fs::SimpleFsFormatOptions{KERNEL_STAGE15_ROOT_INODE_COUNT});
    this->root_file_system_.emplace(this->root_buffer_cache_.value());
    this->vfs_.emplace(this->root_file_system_.value());
}

void Kernel::validate_ipi_route(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core) const
{
    static_cast<void>(this->apic_system_.value().local_apic(source_core));
    static_cast<void>(this->apic_system_.value().local_apic(target_core));
}

const SchedulerHandoff& Kernel::record_scheduler_handoff(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core,
    sched::ThreadContext& thread)
{
    thread.cpu_state().set_core_id(target_core);
    this->scheduler_handoffs_.emplace_back(
        this->next_scheduler_handoff_sequence(),
        source_core,
        target_core,
        thread.id());
    return this->scheduler_handoffs_.back();
}

bool Kernel::send_reschedule_ipi(
    const cpu::system::CoreId source_core,
    const cpu::system::CoreId target_core)
{
    return this->apic_system_.value().send_ipi(source_core, target_core, cpu::system::InterruptVector::reschedule());
}

std::uint64_t Kernel::next_scheduler_handoff_sequence() noexcept
{
    const std::uint64_t sequence = this->next_scheduler_handoff_sequence_;
    ++this->next_scheduler_handoff_sequence_;
    return sequence;
}
}
