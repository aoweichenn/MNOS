#pragma once

#include <cstdint>
#include <vector>

#include <mnos/os/mm/address.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::os::mm
{
class PhysicalPageAllocator final
{
public:
    PhysicalPageAllocator(PageNumber total_page_count, PageNumber first_allocatable_page);

    [[nodiscard]] PageNumber total_page_count() const noexcept;
    [[nodiscard]] PageNumber first_allocatable_page() const noexcept;
    [[nodiscard]] PageNumber free_page_count() const noexcept;
    [[nodiscard]] PageNumber allocated_page_count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] bool contains(PhysicalAddress address) const noexcept;
    [[nodiscard]] bool is_allocated(PhysicalAddress address) const;

    [[nodiscard]] PhysicalAddress allocate_page();
    [[nodiscard]] PhysicalAddress allocate_contiguous(PageNumber page_count);
    void reserve_range(PhysicalAddress start_address, PageNumber page_count);
    void free_page(PhysicalAddress address);

private:
    [[nodiscard]] PageNumber page_index(PhysicalAddress address) const;
    [[nodiscard]] bool is_page_index_allocated(PageNumber page_index) const noexcept;
    void mark_allocated(PageNumber page_index) noexcept;
    void mark_free(PageNumber page_index) noexcept;
    void require_owned_page(PhysicalAddress address) const;
    void require_allocatable_page(PhysicalAddress address) const;
    void require_page_count(PageNumber page_count) const;

    PageNumber total_page_count_;
    PageNumber first_allocatable_page_;
    PageNumber free_page_count_;
    std::vector<std::uint8_t> allocated_pages_;
};
}
