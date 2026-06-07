#pragma once

#include <cstdint>
#include <optional>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/system/trap_frame.hpp>
#include <mnos/os/mm/address.hpp>
#include <mnos/os/mm/page.hpp>
#include <mnos/os/sched/thread_id.hpp>
#include <mnos/os/sched/thread_state.hpp>

namespace mnos::os::sched
{
inline constexpr std::uint64_t THREAD_CONTEXT_DEFAULT_KERNEL_STACK_PAGE_COUNT = std::uint64_t{4};
inline constexpr std::uint64_t THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES =
    THREAD_CONTEXT_DEFAULT_KERNEL_STACK_PAGE_COUNT * mm::MM_PAGE_SIZE_BYTES;

class ThreadContext final
{
public:
    ThreadContext(ThreadId id, mm::VirtualAddress kernel_stack_bottom);
    ThreadContext(ThreadId id, mm::VirtualAddress kernel_stack_bottom, std::uint64_t kernel_stack_size_bytes);

    [[nodiscard]] ThreadId id() const noexcept;
    [[nodiscard]] ThreadState state() const noexcept;
    void set_state(ThreadState state);

    [[nodiscard]] bool is_runnable() const noexcept;
    [[nodiscard]] bool is_alive() const noexcept;

    [[nodiscard]] cpu::CpuState& cpu_state() noexcept;
    [[nodiscard]] const cpu::CpuState& cpu_state() const noexcept;

    [[nodiscard]] mm::VirtualAddress kernel_stack_bottom() const noexcept;
    [[nodiscard]] mm::VirtualAddress kernel_stack_top() const noexcept;
    [[nodiscard]] std::uint64_t kernel_stack_size_bytes() const noexcept;
    [[nodiscard]] bool contains_kernel_stack_address(mm::VirtualAddress address) const noexcept;

    [[nodiscard]] bool has_last_trap_frame() const noexcept;
    [[nodiscard]] const cpu::system::TrapFrame& last_trap_frame() const;
    [[nodiscard]] bool snapshot_pending_trap_frame();
    void clear_last_trap_frame() noexcept;

    void reset_cpu_state();

private:
    void initialize_cpu_stack_pointer();

    ThreadId id_;
    ThreadState state_ = ThreadState::READY;
    cpu::CpuState cpu_state_;
    mm::VirtualAddress kernel_stack_bottom_;
    mm::VirtualAddress kernel_stack_top_;
    std::uint64_t kernel_stack_size_bytes_;
    std::optional<cpu::system::TrapFrame> last_trap_frame_;
};
}
