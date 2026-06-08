#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include <mnos/os/io/file_descriptor.hpp>
#include <mnos/os/mm/address_space.hpp>
#include <mnos/os/proc/process_id.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::proc
{
class Process final
{
public:
    Process(ProcessId id, mm::AddressSpace address_space);

    [[nodiscard]] ProcessId id() const noexcept;
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

private:
    [[nodiscard]] bool contains_thread_id(sched::ThreadId thread_id) const noexcept;

    ProcessId id_;
    mm::AddressSpace address_space_;
    io::FileDescriptorTable file_descriptors_;
    std::deque<sched::ThreadContext> threads_;
};
}
