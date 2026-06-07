#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include <mnos/os/kernel/boot_context.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/mm/page_fault_handler.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/round_robin_scheduler.hpp>

namespace mnos::os::kernel
{
inline constexpr std::uint64_t KERNEL_RESERVED_LOW_PAGE_COUNT = std::uint64_t{1};
inline constexpr std::uint64_t KERNEL_MIN_BOOTABLE_PAGE_COUNT = KERNEL_RESERVED_LOW_PAGE_COUNT + std::uint64_t{1};
inline constexpr std::uint64_t KERNEL_DEFAULT_TABLE_ARENA_PAGE_COUNT = std::uint64_t{8};
inline constexpr mm::AddressValue KERNEL_DEFAULT_THREAD_STACK_BASE = mm::AddressValue{0x7000'0000};
inline constexpr mm::AddressValue KERNEL_THREAD_STACK_STRIDE = mm::MM_PAGE_SIZE_BYTES * mm::AddressValue{16};

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

    [[nodiscard]] mm::PhysicalPageAllocator& physical_page_allocator();
    [[nodiscard]] const mm::PhysicalPageAllocator& physical_page_allocator() const;
    [[nodiscard]] mm::AddressSpace& kernel_address_space();
    [[nodiscard]] const mm::AddressSpace& kernel_address_space() const;
    [[nodiscard]] sched::RoundRobinScheduler& scheduler() noexcept;
    [[nodiscard]] const sched::RoundRobinScheduler& scheduler() const noexcept;

    [[nodiscard]] proc::Process& create_process();
    [[nodiscard]] sched::ThreadContext& create_thread(proc::Process& process);
    [[nodiscard]] sched::ThreadContext& create_thread(
        proc::Process& process,
        mm::VirtualAddress kernel_stack_bottom,
        std::uint64_t kernel_stack_size_bytes = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);
    [[nodiscard]] std::size_t process_count() const noexcept;
    [[nodiscard]] proc::Process& process_at(std::size_t index);
    [[nodiscard]] const proc::Process& process_at(std::size_t index) const;

    [[nodiscard]] mm::PageFaultResult handle_page_fault(sched::ThreadContext& thread);
    [[nodiscard]] mm::PageFaultResult handle_page_fault(proc::Process& process, sched::ThreadContext& thread);
    [[nodiscard]] SyscallResult dispatch_syscall(sched::ThreadContext& thread);

private:
    [[nodiscard]] mm::AddressSpace create_address_space(std::uint64_t table_arena_page_count);
    [[nodiscard]] mm::VirtualAddress next_kernel_stack_bottom() noexcept;
    void require_booted() const;
    void require_stage5_services() const;

    BootContext* boot_context_;
    std::optional<mm::PhysicalPageAllocator> physical_page_allocator_;
    std::optional<mm::AddressSpace> kernel_address_space_;
    sched::RoundRobinScheduler scheduler_;
    std::deque<proc::Process> processes_;
    proc::ProcessId::value_type next_process_id_value_ = proc::PROCESS_ID_FIRST_USER_VALUE;
    sched::ThreadId::value_type next_thread_id_value_ = sched::THREAD_ID_FIRST_KERNEL_VALUE;
    mm::AddressValue next_kernel_stack_bottom_value_ = KERNEL_DEFAULT_THREAD_STACK_BASE;
    bool booted_ = false;
};
}
