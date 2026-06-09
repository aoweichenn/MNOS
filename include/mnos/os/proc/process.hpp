#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string_view>

#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::proc
{
enum class ProcessState : std::uint8_t
{
    RUNNING,
    EXITED,
    REAPED,
    COUNT
};

inline constexpr std::size_t PROCESS_STATE_COUNT = static_cast<std::size_t>(ProcessState::COUNT);

[[nodiscard]] bool is_process_state_valid(ProcessState state) noexcept;
[[nodiscard]] std::size_t process_state_to_index(ProcessState state) noexcept;
[[nodiscard]] std::string_view process_state_to_name(ProcessState state) noexcept;

class Process final
{
public:
    Process(ProcessId id, mm::AddressSpace address_space);
    Process(ProcessId id, mm::AddressSpace address_space, ProcessId parent_id);

    [[nodiscard]] ProcessId id() const noexcept;
    [[nodiscard]] ProcessId parent_id() const noexcept;
    [[nodiscard]] bool has_parent() const noexcept;
    [[nodiscard]] ProcessState state() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool is_exited() const noexcept;
    [[nodiscard]] bool is_reaped() const noexcept;
    [[nodiscard]] std::int64_t exit_code() const noexcept;
    [[nodiscard]] mm::AddressSpace& address_space() noexcept;
    [[nodiscard]] const mm::AddressSpace& address_space() const noexcept;
    [[nodiscard]] io::FileDescriptorTable& file_descriptors() noexcept;
    [[nodiscard]] const io::FileDescriptorTable& file_descriptors() const noexcept;

    [[nodiscard]] sched::ThreadContext& create_thread(
        sched::ThreadId thread_id,
        mm::VirtualAddress kernel_stack_bottom,
        std::uint64_t kernel_stack_size_bytes = sched::THREAD_CONTEXT_DEFAULT_KERNEL_STACK_SIZE_BYTES);

    [[nodiscard]] std::size_t thread_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] sched::ThreadContext& thread_at(std::size_t index);
    [[nodiscard]] const sched::ThreadContext& thread_at(std::size_t index) const;
    void mark_exited(std::int64_t exit_code);
    void mark_reaped();

private:
    [[nodiscard]] bool contains_thread_id(sched::ThreadId thread_id) const noexcept;

    ProcessId id_;
    ProcessId parent_id_;
    ProcessState state_ = ProcessState::RUNNING;
    std::int64_t exit_code_ = std::int64_t{0};
    mm::AddressSpace address_space_;
    io::FileDescriptorTable file_descriptors_;
    std::deque<sched::ThreadContext> threads_;
};
}
