#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mnos/cpu/execution/cpu_state.hpp>
#include <mnos/cpu/memory/memory_bus.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/os/mm/physical_page_allocator.hpp>
#include <mnos/os/proc/process.hpp>

namespace mnos::os::proc
{
enum class CowFaultResult : std::uint8_t
{
    COPIED,
    RESTORED,
    NOT_COW,
    OUT_OF_MEMORY,
    INVALID_FAULT,
    COUNT
};

class CopyOnWriteManager final
{
public:
    [[nodiscard]] std::size_t mapping_count() const noexcept;
    [[nodiscard]] std::size_t frame_ref_count(mm::PhysicalAddress physical_frame) const noexcept;
    [[nodiscard]] bool contains(ProcessId process_id, mm::VirtualAddress virtual_page) const noexcept;

    [[nodiscard]] std::size_t share_pages(
        Process& parent_process,
        Process& child_process,
        std::span<const mm::VirtualAddress> virtual_pages);
    [[nodiscard]] CowFaultResult resolve_write_fault(
        mm::PhysicalPageAllocator& allocator,
        cpu::MemoryBus& memory_bus,
        Process& process,
        cpu::CpuState& cpu_state);

private:
    class CowMapping final
    {
    public:
        CowMapping(
            ProcessId process_id,
            mm::VirtualAddress virtual_page,
            mm::PhysicalAddress physical_frame,
            cpu::memory::PagePermissions restored_permissions) noexcept;

        [[nodiscard]] ProcessId process_id() const noexcept;
        [[nodiscard]] mm::VirtualAddress virtual_page() const noexcept;
        [[nodiscard]] mm::PhysicalAddress physical_frame() const noexcept;
        [[nodiscard]] cpu::memory::PagePermissions restored_permissions() const noexcept;

    private:
        ProcessId process_id_;
        mm::VirtualAddress virtual_page_;
        mm::PhysicalAddress physical_frame_;
        cpu::memory::PagePermissions restored_permissions_;
    };

    class CowFrameRef final
    {
    public:
        explicit CowFrameRef(mm::PhysicalAddress physical_frame) noexcept;

        [[nodiscard]] mm::PhysicalAddress physical_frame() const noexcept;
        [[nodiscard]] std::size_t count() const noexcept;
        void increment() noexcept;
        void decrement() noexcept;

    private:
        mm::PhysicalAddress physical_frame_;
        std::size_t count_ = 1;
    };

    [[nodiscard]] CowMapping* find_mapping(ProcessId process_id, mm::VirtualAddress virtual_page) noexcept;
    [[nodiscard]] const CowMapping* find_mapping(ProcessId process_id, mm::VirtualAddress virtual_page) const noexcept;
    [[nodiscard]] CowFrameRef* find_frame_ref(mm::PhysicalAddress physical_frame) noexcept;
    [[nodiscard]] const CowFrameRef* find_frame_ref(mm::PhysicalAddress physical_frame) const noexcept;
    void add_frame_ref(mm::PhysicalAddress physical_frame);
    void release_frame_ref(mm::PhysicalAddress physical_frame) noexcept;
    void remove_mapping(ProcessId process_id, mm::VirtualAddress virtual_page) noexcept;
    void copy_physical_page(
        cpu::MemoryBus& memory_bus,
        mm::PhysicalAddress source_frame,
        mm::PhysicalAddress target_frame);

    std::vector<CowMapping> mappings_;
    std::vector<CowFrameRef> frame_refs_;
};
}
