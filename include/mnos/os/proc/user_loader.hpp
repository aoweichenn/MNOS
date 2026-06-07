#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/proc/process.hpp>
#include <mnos/os/sched/thread_context.hpp>

namespace mnos::os::proc
{
inline constexpr mm::AddressValue USER_PROGRAM_DEFAULT_STACK_SIZE_BYTES =
    mm::ADDRESS_LAYOUT_USER_STACK_DEFAULT_SIZE_BYTES;

class UserSegment final
{
public:
    UserSegment(
        mm::VirtualAddress virtual_address,
        std::vector<cpu::Byte> bytes,
        mm::AddressValue memory_size_bytes,
        cpu::memory::PagePermissions permissions);

    [[nodiscard]] static UserSegment text(mm::VirtualAddress virtual_address, std::vector<cpu::Byte> bytes);
    [[nodiscard]] static UserSegment data(mm::VirtualAddress virtual_address, std::vector<cpu::Byte> bytes);
    [[nodiscard]] static UserSegment bss(
        mm::VirtualAddress virtual_address,
        mm::AddressValue memory_size_bytes,
        cpu::memory::PagePermissions permissions);

    [[nodiscard]] mm::VirtualAddress virtual_address() const noexcept;
    [[nodiscard]] mm::VirtualAddress end_address() const noexcept;
    [[nodiscard]] std::span<const cpu::Byte> bytes() const noexcept;
    [[nodiscard]] mm::AddressValue memory_size_bytes() const noexcept;
    [[nodiscard]] cpu::memory::PagePermissions permissions() const noexcept;
    [[nodiscard]] bool contains(mm::VirtualAddress address) const noexcept;

private:
    mm::VirtualAddress virtual_address_;
    std::vector<cpu::Byte> bytes_;
    mm::AddressValue memory_size_bytes_;
    cpu::memory::PagePermissions permissions_;
};

class UserProgram final
{
public:
    explicit UserProgram(mm::VirtualAddress entry_point);

    [[nodiscard]] mm::VirtualAddress entry_point() const noexcept;
    [[nodiscard]] mm::AddressValue initial_stack_size_bytes() const noexcept;
    void set_initial_stack_size_bytes(mm::AddressValue stack_size_bytes);

    void add_segment(UserSegment segment);
    [[nodiscard]] std::span<const UserSegment> segments() const noexcept;
    [[nodiscard]] bool entry_point_is_executable() const noexcept;

private:
    void ensure_segment_does_not_overlap(const UserSegment& segment) const;

    mm::VirtualAddress entry_point_;
    mm::AddressValue initial_stack_size_bytes_ = USER_PROGRAM_DEFAULT_STACK_SIZE_BYTES;
    std::vector<UserSegment> segments_;
};

class UserProcessImage final
{
public:
    UserProcessImage(
        mm::VirtualAddress entry_point,
        mm::VirtualAddress stack_bottom,
        mm::VirtualAddress initial_stack_pointer,
        std::size_t mapped_page_count) noexcept;

    [[nodiscard]] mm::VirtualAddress entry_point() const noexcept;
    [[nodiscard]] mm::VirtualAddress stack_bottom() const noexcept;
    [[nodiscard]] mm::VirtualAddress initial_stack_pointer() const noexcept;
    [[nodiscard]] std::size_t mapped_page_count() const noexcept;

private:
    mm::VirtualAddress entry_point_;
    mm::VirtualAddress stack_bottom_;
    mm::VirtualAddress initial_stack_pointer_;
    std::size_t mapped_page_count_;
};

class UserLoader final
{
public:
    UserLoader(mm::PhysicalPageAllocator& allocator, cpu::MemoryBus& memory_bus) noexcept;

    [[nodiscard]] UserProcessImage load(const UserProgram& program, Process& process);
    void initialize_user_thread(
        const UserProcessImage& image,
        Process& process,
        sched::ThreadContext& thread) const;

private:
    [[nodiscard]] static cpu::memory::ProcessContextId process_context_id_for(ProcessId process_id);
    void zero_physical_page(mm::PhysicalAddress physical_address);
    void copy_segment_page(
        const UserSegment& segment,
        mm::AddressValue segment_page_offset,
        mm::PhysicalAddress physical_address);
    [[nodiscard]] mm::PhysicalAddress allocate_zeroed_page();

    mm::PhysicalPageAllocator* allocator_;
    cpu::MemoryBus* memory_bus_;
};
}
