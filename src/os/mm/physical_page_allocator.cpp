#include <stdexcept>

#include <mnos/os/mm/physical_page_allocator.hpp>

namespace
{
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_EMPTY_MEMORY_MESSAGE =
    "physical page allocator requires at least one physical page";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_FIRST_PAGE_OUT_OF_RANGE_MESSAGE =
    "physical page allocator first allocatable page is out of range";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_UNALIGNED_ADDRESS_MESSAGE =
    "physical page allocator address must be page aligned";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_OUT_OF_RANGE_ADDRESS_MESSAGE =
    "physical page allocator address is outside physical memory";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_ZERO_PAGE_COUNT_MESSAGE =
    "physical page allocator page count must be non-zero";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_DOUBLE_FREE_MESSAGE =
    "physical page allocator cannot free an unallocated page";
constexpr const char* PHYSICAL_PAGE_ALLOCATOR_RESERVED_PAGE_MESSAGE =
    "physical page allocator cannot free a reserved page";
}

namespace mnos::os::mm
{
PhysicalPageAllocator::PhysicalPageAllocator(
    const PageNumber total_page_count,
    const PageNumber first_allocatable_page) :
    total_page_count_(total_page_count),
    first_allocatable_page_(first_allocatable_page),
    free_page_count_(total_page_count > first_allocatable_page ? total_page_count - first_allocatable_page : PageNumber{0}),
    allocated_pages_(static_cast<std::size_t>(total_page_count), std::uint8_t{1})
{
    if (this->total_page_count_ == PageNumber{0})
    {
        throw std::invalid_argument{PHYSICAL_PAGE_ALLOCATOR_EMPTY_MEMORY_MESSAGE};
    }

    if (this->first_allocatable_page_ > this->total_page_count_)
    {
        throw std::out_of_range{PHYSICAL_PAGE_ALLOCATOR_FIRST_PAGE_OUT_OF_RANGE_MESSAGE};
    }

    for (PageNumber page_index = this->first_allocatable_page_; page_index < this->total_page_count_; ++page_index)
    {
        this->allocated_pages_[static_cast<std::size_t>(page_index)] = std::uint8_t{0};
    }
}

PageNumber PhysicalPageAllocator::total_page_count() const noexcept
{
    return this->total_page_count_;
}

PageNumber PhysicalPageAllocator::first_allocatable_page() const noexcept
{
    return this->first_allocatable_page_;
}

PageNumber PhysicalPageAllocator::free_page_count() const noexcept
{
    return this->free_page_count_;
}

PageNumber PhysicalPageAllocator::allocated_page_count() const noexcept
{
    return this->total_page_count_ - this->free_page_count_;
}

bool PhysicalPageAllocator::empty() const noexcept
{
    return this->free_page_count_ == PageNumber{0};
}

bool PhysicalPageAllocator::contains(const PhysicalAddress address) const noexcept
{
    return is_page_aligned(address) && page_number(address) < this->total_page_count_;
}

bool PhysicalPageAllocator::is_allocated(const PhysicalAddress address) const
{
    this->require_owned_page(address);
    return this->is_page_index_allocated(this->page_index(address));
}

PhysicalAddress PhysicalPageAllocator::allocate_page()
{
    return this->allocate_contiguous(PageNumber{1});
}

PhysicalAddress PhysicalPageAllocator::allocate_contiguous(const PageNumber page_count)
{
    this->require_page_count(page_count);
    if (page_count > this->free_page_count_)
    {
        throw std::bad_alloc{};
    }

    PageNumber current_run_start = PageNumber{0};
    PageNumber current_run_length = PageNumber{0};
    for (PageNumber page_index = this->first_allocatable_page_; page_index < this->total_page_count_; ++page_index)
    {
        if (this->is_page_index_allocated(page_index))
        {
            current_run_length = PageNumber{0};
            continue;
        }

        if (current_run_length == PageNumber{0})
        {
            current_run_start = page_index;
        }
        ++current_run_length;

        if (current_run_length == page_count)
        {
            for (PageNumber allocated_index = current_run_start;
                 allocated_index < current_run_start + page_count;
                 ++allocated_index)
            {
                this->mark_allocated(allocated_index);
            }
            return PhysicalAddress{current_run_start * MM_PAGE_SIZE_BYTES};
        }
    }

    throw std::bad_alloc{};
}

void PhysicalPageAllocator::reserve_range(const PhysicalAddress start_address, const PageNumber page_count)
{
    this->require_owned_page(start_address);
    this->require_page_count(page_count);
    const PageNumber start_index = this->page_index(start_address);
    if (page_count > this->total_page_count_ - start_index)
    {
        throw std::out_of_range{PHYSICAL_PAGE_ALLOCATOR_OUT_OF_RANGE_ADDRESS_MESSAGE};
    }

    for (PageNumber page_index = start_index; page_index < start_index + page_count; ++page_index)
    {
        if (!this->is_page_index_allocated(page_index))
        {
            this->mark_allocated(page_index);
        }
    }
}

void PhysicalPageAllocator::free_page(const PhysicalAddress address)
{
    this->require_allocatable_page(address);
    const PageNumber index = this->page_index(address);
    if (!this->is_page_index_allocated(index))
    {
        throw std::logic_error{PHYSICAL_PAGE_ALLOCATOR_DOUBLE_FREE_MESSAGE};
    }
    this->mark_free(index);
}

PageNumber PhysicalPageAllocator::page_index(const PhysicalAddress address) const
{
    this->require_owned_page(address);
    return page_number(address);
}

bool PhysicalPageAllocator::is_page_index_allocated(const PageNumber page_index) const noexcept
{
    return this->allocated_pages_[static_cast<std::size_t>(page_index)] != std::uint8_t{0};
}

void PhysicalPageAllocator::mark_allocated(const PageNumber page_index) noexcept
{
    if (!this->is_page_index_allocated(page_index))
    {
        this->allocated_pages_[static_cast<std::size_t>(page_index)] = std::uint8_t{1};
        --this->free_page_count_;
    }
}

void PhysicalPageAllocator::mark_free(const PageNumber page_index) noexcept
{
    if (this->is_page_index_allocated(page_index))
    {
        this->allocated_pages_[static_cast<std::size_t>(page_index)] = std::uint8_t{0};
        ++this->free_page_count_;
    }
}

void PhysicalPageAllocator::require_owned_page(const PhysicalAddress address) const
{
    if (!is_page_aligned(address))
    {
        throw std::invalid_argument{PHYSICAL_PAGE_ALLOCATOR_UNALIGNED_ADDRESS_MESSAGE};
    }

    if (page_number(address) >= this->total_page_count_)
    {
        throw std::out_of_range{PHYSICAL_PAGE_ALLOCATOR_OUT_OF_RANGE_ADDRESS_MESSAGE};
    }
}

void PhysicalPageAllocator::require_allocatable_page(const PhysicalAddress address) const
{
    this->require_owned_page(address);
    if (this->page_index(address) < this->first_allocatable_page_)
    {
        throw std::logic_error{PHYSICAL_PAGE_ALLOCATOR_RESERVED_PAGE_MESSAGE};
    }
}

void PhysicalPageAllocator::require_page_count(const PageNumber page_count) const
{
    if (page_count == PageNumber{0})
    {
        throw std::invalid_argument{PHYSICAL_PAGE_ALLOCATOR_ZERO_PAGE_COUNT_MESSAGE};
    }
}
}
