#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/cpu/execution/trace.hpp>
#include <mnos/cpu/memory/mmu.hpp>
#include <mnos/cpu/memory/tlb_shootdown.hpp>
#include <mnos/cpu/system/apic.hpp>
#include <mnos/os/block/block_device.hpp>
#include <mnos/os/block/buffer_cache.hpp>
#include <mnos/os/fs/vfs.hpp>
#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page_fault_handler.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/proc/copy_on_write.hpp>
#include <mnos/os/proc/futex.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/proc/user_loader.hpp>
#include <mnos/os/sched/round_robin_scheduler.hpp>
#include <mnos/os/sched/sleep_queue.hpp>
#include <mnos/os/sched/smp_scheduler.hpp>
#include <mnos/os/sched/wait_queue.hpp>
#include <mnos/os/tty/console.hpp>

namespace mnos::cpu::system
{
class TrapController;
}

namespace mnos::os::kernel
{
inline constexpr std::uint64_t KERNEL_RESERVED_LOW_PAGE_COUNT = std::uint64_t{1};
inline constexpr std::uint64_t KERNEL_MIN_BOOTABLE_PAGE_COUNT = KERNEL_RESERVED_LOW_PAGE_COUNT + std::uint64_t{1};
inline constexpr std::uint64_t KERNEL_DEFAULT_TABLE_ARENA_PAGE_COUNT = std::uint64_t{16};
inline constexpr mm::AddressValue KERNEL_DEFAULT_THREAD_STACK_BASE = mm::AddressValue{0x7000'0000};
inline constexpr mm::AddressValue KERNEL_THREAD_STACK_STRIDE = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{16};
inline constexpr sched::SchedulerTick KERNEL_STAGE7_DEFAULT_TIMER_INTERVAL_TICKS = sched::SchedulerTick{1};
inline constexpr std::uint64_t KERNEL_SCHEDULER_HANDOFF_FIRST_SEQUENCE = std::uint64_t{1};
inline constexpr std::uint64_t KERNEL_STAGE15_ROOT_BLOCK_COUNT = std::uint64_t{4096};
inline constexpr std::size_t KERNEL_STAGE15_BUFFER_CACHE_BLOCKS = std::size_t{64};
inline constexpr std::uint32_t KERNEL_STAGE15_ROOT_INODE_COUNT = std::uint32_t{128};
inline constexpr std::size_t KERNEL_USER_EXEC_DEFAULT_MAX_STEPS = std::size_t{256};
inline constexpr std::int64_t KERNEL_USER_TRAP_KILLED_EXIT_CODE = std::int64_t{-1};

enum class UserTrapResult : std::uint8_t
{
    HANDLED,
    NOT_USER_TRAP,
    KILLED,
    OUT_OF_MEMORY,
    COUNT
};

enum class UserMapResult : std::uint8_t
{
    MAPPED,
    INVALID_ADDRESS,
    ALREADY_MAPPED,
    OUT_OF_MEMORY,
    COUNT
};

enum class ProcessWaitStatus : std::uint8_t
{
    EXITED,
    BLOCKED,
    NO_CHILD,
    COUNT
};

enum class UserProcessRunStatus : std::uint8_t
{
    EXITED,
    BLOCKED,
    KILLED,
    OUT_OF_MEMORY,
    MAX_STEPS,
    COUNT
};

inline constexpr std::size_t PROCESS_WAIT_STATUS_COUNT =
    static_cast<std::size_t>(ProcessWaitStatus::COUNT);
inline constexpr std::size_t USER_PROCESS_RUN_STATUS_COUNT =
    static_cast<std::size_t>(UserProcessRunStatus::COUNT);

[[nodiscard]] bool is_process_wait_status_valid(ProcessWaitStatus status) noexcept;
[[nodiscard]] std::size_t process_wait_status_to_index(ProcessWaitStatus status) noexcept;
[[nodiscard]] std::string_view process_wait_status_to_name(ProcessWaitStatus status) noexcept;
[[nodiscard]] bool is_user_process_run_status_valid(UserProcessRunStatus status) noexcept;
[[nodiscard]] std::size_t user_process_run_status_to_index(UserProcessRunStatus status) noexcept;
[[nodiscard]] std::string_view user_process_run_status_to_name(UserProcessRunStatus status) noexcept;

class ProcessWaitResult final
{
public:
    [[nodiscard]] static ProcessWaitResult exited(proc::ProcessId child_id, std::int64_t exit_code) noexcept;
    [[nodiscard]] static ProcessWaitResult blocked(proc::ProcessId child_id) noexcept;
    [[nodiscard]] static ProcessWaitResult no_child(proc::ProcessId child_id) noexcept;

    [[nodiscard]] ProcessWaitStatus status() const noexcept;
    [[nodiscard]] proc::ProcessId child_id() const noexcept;
    [[nodiscard]] std::int64_t exit_code() const noexcept;
    [[nodiscard]] bool has_exit_code() const noexcept;

private:
    ProcessWaitResult(
        ProcessWaitStatus status,
        proc::ProcessId child_id,
        std::int64_t exit_code,
        bool has_exit_code) noexcept;

    ProcessWaitStatus status_;
    proc::ProcessId child_id_;
    std::int64_t exit_code_;
    bool has_exit_code_;
};

class UserProcessRunResult final
{
public:
    UserProcessRunResult(
        proc::ProcessId process_id,
        UserProcessRunStatus status,
        std::size_t executed_step_count,
        std::int64_t exit_code,
        bool has_exit_code,
        cpu::ExecutionTrace trace) noexcept;

    [[nodiscard]] proc::ProcessId process_id() const noexcept;
    [[nodiscard]] UserProcessRunStatus status() const noexcept;
    [[nodiscard]] std::size_t executed_step_count() const noexcept;
    [[nodiscard]] std::int64_t exit_code() const noexcept;
    [[nodiscard]] bool has_exit_code() const noexcept;
    [[nodiscard]] const cpu::ExecutionTrace& trace() const noexcept;

private:
    proc::ProcessId process_id_;
    UserProcessRunStatus status_;
    std::size_t executed_step_count_;
    std::int64_t exit_code_;
    bool has_exit_code_;
    cpu::ExecutionTrace trace_;
};

class SchedulerHandoff final
{
public:
    SchedulerHandoff(
        std::uint64_t sequence,
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        sched::ThreadId thread_id) noexcept;

    [[nodiscard]] std::uint64_t sequence() const noexcept;
    [[nodiscard]] cpu::system::CoreId source_core() const noexcept;
    [[nodiscard]] cpu::system::CoreId target_core() const noexcept;
    [[nodiscard]] sched::ThreadId thread_id() const noexcept;

private:
    std::uint64_t sequence_;
    cpu::system::CoreId source_core_;
    cpu::system::CoreId target_core_;
    sched::ThreadId thread_id_;
};

class Kernel final
{
public:
    explicit Kernel(BootContext& boot_context) noexcept;

    void boot();

    [[nodiscard]] bool is_booted() const noexcept;
    [[nodiscard]] BootContext& boot_context() noexcept;
    [[nodiscard]] const BootContext& boot_context() const noexcept;
    [[nodiscard]] std::size_t physical_memory_size_bytes() const noexcept;
    [[nodiscard]] std::uint64_t physical_page_count() const noexcept;
    [[nodiscard]] std::uint32_t bootstrap_processor_count() const noexcept;
    [[nodiscard]] bool has_stage5_services() const noexcept;
    [[nodiscard]] bool has_stage7_services() const noexcept;
    [[nodiscard]] bool has_stage9_services() const noexcept;
    [[nodiscard]] bool has_stage10_services() const noexcept;
    [[nodiscard]] bool has_stage11_services() const noexcept;
    [[nodiscard]] bool has_stage12_services() const noexcept;
    [[nodiscard]] bool has_stage13_services() const noexcept;
    [[nodiscard]] bool has_stage15_services() const noexcept;

    [[nodiscard]] mm::PhysicalPageAllocator& physical_page_allocator();
    [[nodiscard]] const mm::PhysicalPageAllocator& physical_page_allocator() const;
    [[nodiscard]] mm::AddressSpace& kernel_address_space();
    [[nodiscard]] const mm::AddressSpace& kernel_address_space() const;
    [[nodiscard]] sched::RoundRobinScheduler& scheduler() noexcept;
    [[nodiscard]] const sched::RoundRobinScheduler& scheduler() const noexcept;
    [[nodiscard]] cpu::system::ApicSystem& apic_system();
    [[nodiscard]] const cpu::system::ApicSystem& apic_system() const;
    [[nodiscard]] sched::SmpScheduler& smp_scheduler();
    [[nodiscard]] const sched::SmpScheduler& smp_scheduler() const;
    [[nodiscard]] sched::SleepQueue& sleep_queue() noexcept;
    [[nodiscard]] const sched::SleepQueue& sleep_queue() const noexcept;
    [[nodiscard]] cpu::memory::TlbShootdownController& tlb_shootdown_controller() noexcept;
    [[nodiscard]] const cpu::memory::TlbShootdownController& tlb_shootdown_controller() const noexcept;
    [[nodiscard]] proc::CopyOnWriteManager& copy_on_write_manager() noexcept;
    [[nodiscard]] const proc::CopyOnWriteManager& copy_on_write_manager() const noexcept;
    [[nodiscard]] proc::FutexTable& futex_table() noexcept;
    [[nodiscard]] const proc::FutexTable& futex_table() const noexcept;
    [[nodiscard]] tty::Console& console() noexcept;
    [[nodiscard]] const tty::Console& console() const noexcept;
    [[nodiscard]] fs::Vfs& vfs();
    [[nodiscard]] const fs::Vfs& vfs() const;

    [[nodiscard]] proc::Process& create_process();
    [[nodiscard]] sched::ThreadContext& create_thread(proc::Process& process);
    [[nodiscard]] sched::ThreadContext& create_thread_on_core(
        proc::Process& process,
        cpu::system::CoreId target_core);
    [[nodiscard]] sched::ThreadContext& create_thread(
        proc::Process& process,
        mm::VirtualAddress kernel_stack_bottom,
        std::uint64_t kernel_stack_size_bytes = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);
    [[nodiscard]] sched::ThreadContext& create_thread_on_core(
        proc::Process& process,
        cpu::system::CoreId target_core,
        mm::VirtualAddress kernel_stack_bottom,
        std::uint64_t kernel_stack_size_bytes = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);
    [[nodiscard]] proc::UserProcessImage load_user_program(proc::Process& process, const proc::UserProgram& program);
    [[nodiscard]] sched::ThreadContext& create_user_thread(
        proc::Process& process,
        const proc::UserProcessImage& image);
    [[nodiscard]] proc::Process& create_user_process(const proc::UserProgram& program);
    [[nodiscard]] proc::Process& create_user_process(
        const proc::UserProgram& program,
        proc::ProcessId parent_id);
    [[nodiscard]] UserProcessRunResult exec_user_program(
        proc::ProcessId parent_id,
        const proc::UserProgram& program,
        const cpu::ExecutableImage& image,
        std::size_t max_steps = KERNEL_USER_EXEC_DEFAULT_MAX_STEPS);
    void exit_process(proc::Process& process, std::int64_t exit_code);
    [[nodiscard]] ProcessWaitResult wait_process(
        proc::Process& parent_process,
        sched::ThreadContext& waiter,
        proc::ProcessId child_id);
    [[nodiscard]] bool has_last_user_execution_trace() const noexcept;
    [[nodiscard]] cpu::ExecutionTrace take_last_user_execution_trace();
    [[nodiscard]] proc::Process& fork_process_cow(
        proc::Process& parent_process,
        std::span<const mm::VirtualAddress> cow_pages);
    [[nodiscard]] std::size_t process_count() const noexcept;
    [[nodiscard]] proc::Process& process_at(std::size_t index);
    [[nodiscard]] const proc::Process& process_at(std::size_t index) const;

    [[nodiscard]] mm::PageFaultResult handle_page_fault(sched::ThreadContext& thread);
    [[nodiscard]] mm::PageFaultResult handle_page_fault(proc::Process& process, sched::ThreadContext& thread);
    [[nodiscard]] SyscallResult dispatch_syscall(sched::ThreadContext& thread);
    [[nodiscard]] SyscallResult dispatch_syscall(proc::Process& process, sched::ThreadContext& thread);
    [[nodiscard]] SyscallResult handle_user_syscall(
        proc::Process& process,
        sched::ThreadContext& thread,
        cpu::system::TrapController& trap_controller);
    [[nodiscard]] UserTrapResult handle_user_page_fault(
        proc::Process& process,
        sched::ThreadContext& thread,
        cpu::system::TrapController& trap_controller);
    [[nodiscard]] UserMapResult map_user_zero_page(
        proc::Process& process,
        sched::ThreadContext& thread,
        mm::VirtualAddress virtual_page);
    void console_write(std::string_view text);
    [[nodiscard]] tty::ConsoleReadResult console_read(
        sched::ThreadContext& thread,
        std::span<char> destination);
    [[nodiscard]] std::vector<sched::ThreadContext*> submit_terminal_input(std::string_view text);
    [[nodiscard]] io::IoResult read_fd(
        proc::Process& process,
        sched::ThreadContext& thread,
        io::FileDescriptor descriptor,
        std::span<char> destination);
    [[nodiscard]] io::IoResult write_fd(
        proc::Process& process,
        io::FileDescriptor descriptor,
        std::string_view text);
    [[nodiscard]] io::FileDescriptor open_file(
        proc::Process& process,
        std::string_view path,
        io::FileAccessMode access_mode,
        bool create_if_missing);
    [[nodiscard]] bool close_fd(proc::Process& process, io::FileDescriptor descriptor);
    [[nodiscard]] sched::SchedulerTick scheduler_tick_count() const noexcept;
    [[nodiscard]] std::optional<cpu::system::ApicInterrupt> tick_core_timer(cpu::system::CoreId core_id);
    [[nodiscard]] sched::ThreadContext* handle_timer_interrupt(cpu::system::CoreId core_id);
    [[nodiscard]] sched::ThreadContext* handle_smp_timer_interrupt(cpu::system::CoreId core_id);
    [[nodiscard]] sched::ThreadContext* sleep_current_until(sched::SchedulerTick wake_tick);
    [[nodiscard]] sched::ThreadContext* sleep_current_for(sched::SchedulerTick duration_ticks);
    [[nodiscard]] std::size_t wake_sleepers();
    [[nodiscard]] const cpu::memory::TlbShootdownRequest& request_tlb_shootdown_page(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        cpu::Address64 linear_address,
        std::optional<cpu::memory::ProcessContextId> context_id = std::nullopt);
    [[nodiscard]] const cpu::memory::TlbShootdownRequest& request_tlb_shootdown_all(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        std::optional<cpu::memory::ProcessContextId> context_id = std::nullopt);
    [[nodiscard]] const SchedulerHandoff& request_scheduler_handoff(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        sched::ThreadContext& thread);
    [[nodiscard]] bool wake_thread_on_core(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        sched::ThreadContext& thread);
    [[nodiscard]] const SchedulerHandoff& request_smp_migration(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        sched::ThreadContext& thread);
    [[nodiscard]] std::optional<sched::ThreadMigration> rebalance_smp_once();
    [[nodiscard]] bool apply_next_tlb_shootdown_for_core(
        cpu::system::CoreId core_id,
        cpu::memory::MemoryManagementUnit& mmu);
    [[nodiscard]] proc::CowFaultResult handle_cow_write_fault(
        proc::Process& process,
        sched::ThreadContext& thread);
    [[nodiscard]] sched::ThreadContext* wait_on_futex(
        proc::Process& process,
        mm::VirtualAddress address,
        sched::ThreadContext& thread);
    [[nodiscard]] sched::ThreadContext* wake_one_futex(proc::Process& process, mm::VirtualAddress address);
    [[nodiscard]] std::vector<sched::ThreadContext*> wake_all_futex(
        proc::Process& process,
        mm::VirtualAddress address);
    [[nodiscard]] std::size_t scheduler_handoff_count() const noexcept;
    [[nodiscard]] const SchedulerHandoff& scheduler_handoff_at(std::size_t index) const;

private:
    struct ProcessWaitEntry final
    {
        proc::ProcessId parent_id;
        proc::ProcessId child_id;
        sched::WaitQueue wait_queue;
    };

    [[nodiscard]] proc::Process& create_process_with_parent(proc::ProcessId parent_id);
    [[nodiscard]] mm::AddressSpace create_address_space(std::uint64_t table_arena_page_count);
    [[nodiscard]] mm::VirtualAddress next_kernel_stack_bottom() noexcept;
    void require_booted() const;
    void require_stage5_services() const;
    void require_stage7_services() const;
    void require_stage9_services() const;
    void require_stage10_services() const;
    void require_stage11_services() const;
    void require_stage12_services() const;
    void require_stage13_services() const;
    void require_stage15_services() const;
    void configure_stage7_services();
    void configure_stage9_services();
    void configure_stage15_services();
    [[nodiscard]] SyscallResult dispatch_syscall_for_process(
        proc::Process* process,
        sched::ThreadContext& thread);
    [[nodiscard]] SyscallResult dispatch_getpid(proc::Process* process, SyscallFrame& frame) noexcept;
    [[nodiscard]] SyscallResult dispatch_map_anon_page(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_fork_cow(proc::Process* process, SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_futex_wait(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_futex_wake_one(proc::Process* process, SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_futex_wake_all(proc::Process* process, SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_read(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_write(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_open(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_close(proc::Process* process, SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_stat(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_readdir(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] SyscallResult dispatch_wait(
        proc::Process* process,
        sched::ThreadContext& thread,
        SyscallFrame& frame);
    [[nodiscard]] UserTrapResult kill_user_trap_thread(proc::Process& process, sched::ThreadContext& thread);
    [[nodiscard]] proc::Process* find_process_by_id(proc::ProcessId process_id) noexcept;
    [[nodiscard]] const proc::Process* find_process_by_id(proc::ProcessId process_id) const noexcept;
    [[nodiscard]] ProcessWaitEntry& process_wait_entry(proc::ProcessId parent_id, proc::ProcessId child_id);
    void wake_process_waiters(proc::Process& process);
    void remove_process_wait_entries_for_child(proc::ProcessId child_id);
    void zero_physical_page(mm::PhysicalAddress physical_address);
    void validate_ipi_route(cpu::system::CoreId source_core, cpu::system::CoreId target_core) const;
    [[nodiscard]] const SchedulerHandoff& record_scheduler_handoff(
        cpu::system::CoreId source_core,
        cpu::system::CoreId target_core,
        sched::ThreadContext& thread);
    [[nodiscard]] bool send_reschedule_ipi(cpu::system::CoreId source_core, cpu::system::CoreId target_core);
    [[nodiscard]] std::uint64_t next_scheduler_handoff_sequence() noexcept;

    BootContext* boot_context_;
    std::optional<mm::PhysicalPageAllocator> physical_page_allocator_;
    std::optional<mm::AddressSpace> kernel_address_space_;
    std::optional<cpu::system::ApicSystem> apic_system_;
    std::optional<sched::SmpScheduler> smp_scheduler_;
    sched::RoundRobinScheduler scheduler_;
    sched::SleepQueue sleep_queue_;
    cpu::memory::TlbShootdownController tlb_shootdown_controller_;
    proc::CopyOnWriteManager copy_on_write_manager_;
    proc::FutexTable futex_table_;
    tty::Console console_;
    std::optional<block::MemoryBlockDevice> root_block_device_;
    std::optional<block::BufferCache> root_buffer_cache_;
    std::optional<fs::SimpleFileSystem> root_file_system_;
    std::optional<fs::Vfs> vfs_;
    std::deque<proc::Process> processes_;
    std::deque<ProcessWaitEntry> process_wait_entries_;
    std::deque<SchedulerHandoff> scheduler_handoffs_;
    std::optional<cpu::ExecutionTrace> last_user_execution_trace_;
    proc::ProcessId::value_type next_process_id_value_ = proc::PROCESS_ID_FIRST_USER_VALUE;
    sched::ThreadId::value_type next_thread_id_value_ = sched::THREAD_ID_FIRST_KERNEL_VALUE;
    std::uint64_t next_scheduler_handoff_sequence_ = KERNEL_SCHEDULER_HANDOFF_FIRST_SEQUENCE;
    mm::AddressValue next_kernel_stack_bottom_value_ = KERNEL_DEFAULT_THREAD_STACK_BASE;
    sched::SchedulerTick scheduler_tick_count_ = sched::SchedulerTick{0};
    bool booted_ = false;
};
}
