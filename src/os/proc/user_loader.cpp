#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/os/proc/user_loader.hpp>

namespace
{
constexpr const char* USER_SEGMENT_UNALIGNED_ADDRESS_MESSAGE = "user segment virtual address must be page aligned";
constexpr const char* USER_SEGMENT_EMPTY_MEMORY_MESSAGE = "user segment memory size must not be empty";
constexpr const char* USER_SEGMENT_UNALIGNED_MEMORY_MESSAGE = "user segment memory size must be page aligned";
constexpr const char* USER_SEGMENT_BYTES_OVERFLOW_MESSAGE = "user segment bytes exceed mapped memory size";
constexpr const char* USER_SEGMENT_PERMISSION_MESSAGE = "user segment permissions must be user accessible";
constexpr const char* USER_SEGMENT_RANGE_MESSAGE = "user segment must live inside user address range";
constexpr const char* USER_PROGRAM_ENTRY_MESSAGE = "user program entry point must live inside user address range";
constexpr const char* USER_PROGRAM_STACK_MESSAGE = "user program stack size must be page aligned and non-zero";
constexpr const char* USER_PROGRAM_OVERLAP_MESSAGE = "user program segments must not overlap";
constexpr const char* USER_PROGRAM_ENTRY_EXECUTABLE_MESSAGE =
    "user program entry point must be inside an executable segment";
constexpr const char* USER_LOADER_PCID_MESSAGE = "user process id cannot be represented as an x86-64 pcid";

[[nodiscard]] mnos::os::mm::AddressValue rounded_segment_size_for_bytes(const std::size_t byte_count)
{
    if (byte_count == std::size_t{0})
    {
        return mnos::os::mm::MM_PAGE_SIZE_BYTES;
    }
    return mnos::os::mm::align_up_value(static_cast<mnos::os::mm::AddressValue>(byte_count));
}

[[nodiscard]] bool ranges_overlap(
    const mnos::os::mm::VirtualAddress left_start,
    const mnos::os::mm::VirtualAddress left_end,
    const mnos::os::mm::VirtualAddress right_start,
    const mnos::os::mm::VirtualAddress right_end) noexcept
{
    return left_start < right_end && right_start < left_end;
}
}

namespace mnos::os::proc
{
UserSegment::UserSegment(
    const mm::VirtualAddress virtual_address,
    std::vector<cpu::Byte> bytes,
    const mm::AddressValue memory_size_bytes,
    const cpu::memory::PagePermissions permissions) :
    virtual_address_(virtual_address),
    bytes_(std::move(bytes)),
    memory_size_bytes_(memory_size_bytes),
    permissions_(permissions)
{
    if (!mm::is_page_aligned(this->virtual_address_))
    {
        throw std::invalid_argument{USER_SEGMENT_UNALIGNED_ADDRESS_MESSAGE};
    }
    if (this->memory_size_bytes_ == mm::AddressValue{0})
    {
        throw std::invalid_argument{USER_SEGMENT_EMPTY_MEMORY_MESSAGE};
    }
    if (!mm::is_page_aligned(this->memory_size_bytes_))
    {
        throw std::invalid_argument{USER_SEGMENT_UNALIGNED_MEMORY_MESSAGE};
    }
    if (this->bytes_.size() > this->memory_size_bytes_)
    {
        throw std::invalid_argument{USER_SEGMENT_BYTES_OVERFLOW_MESSAGE};
    }
    if (!this->permissions_.user_accessible())
    {
        throw std::invalid_argument{USER_SEGMENT_PERMISSION_MESSAGE};
    }
    if (!mm::is_user_range(this->virtual_address_, this->memory_size_bytes_))
    {
        throw std::out_of_range{USER_SEGMENT_RANGE_MESSAGE};
    }
}

UserSegment UserSegment::text(mm::VirtualAddress virtual_address, std::vector<cpu::Byte> bytes)
{
    return UserSegment{
        virtual_address,
        std::move(bytes),
        rounded_segment_size_for_bytes(bytes.size()),
        cpu::memory::PagePermissions::user_read_only_execute()};
}

UserSegment UserSegment::data(mm::VirtualAddress virtual_address, std::vector<cpu::Byte> bytes)
{
    return UserSegment{
        virtual_address,
        std::move(bytes),
        rounded_segment_size_for_bytes(bytes.size()),
        cpu::memory::PagePermissions::user_read_write_no_execute()};
}

UserSegment UserSegment::bss(
    const mm::VirtualAddress virtual_address,
    const mm::AddressValue memory_size_bytes,
    const cpu::memory::PagePermissions permissions)
{
    return UserSegment{virtual_address, std::vector<cpu::Byte>{}, memory_size_bytes, permissions};
}

mm::VirtualAddress UserSegment::virtual_address() const noexcept
{
    return this->virtual_address_;
}

mm::VirtualAddress UserSegment::end_address() const noexcept
{
    return this->virtual_address_ + this->memory_size_bytes_;
}

std::span<const cpu::Byte> UserSegment::bytes() const noexcept
{
    return std::span<const cpu::Byte>{this->bytes_};
}

mm::AddressValue UserSegment::memory_size_bytes() const noexcept
{
    return this->memory_size_bytes_;
}

cpu::memory::PagePermissions UserSegment::permissions() const noexcept
{
    return this->permissions_;
}

bool UserSegment::contains(const mm::VirtualAddress address) const noexcept
{
    return this->virtual_address_ <= address && address < this->end_address();
}

UserProgram::UserProgram(const mm::VirtualAddress entry_point) : entry_point_(entry_point)
{
    if (!mm::is_user_address(this->entry_point_))
    {
        throw std::out_of_range{USER_PROGRAM_ENTRY_MESSAGE};
    }
}

mm::VirtualAddress UserProgram::entry_point() const noexcept
{
    return this->entry_point_;
}

mm::AddressValue UserProgram::initial_stack_size_bytes() const noexcept
{
    return this->initial_stack_size_bytes_;
}

void UserProgram::set_initial_stack_size_bytes(const mm::AddressValue stack_size_bytes)
{
    if (stack_size_bytes == mm::AddressValue{0} || !mm::is_page_aligned(stack_size_bytes))
    {
        throw std::invalid_argument{USER_PROGRAM_STACK_MESSAGE};
    }
    static_cast<void>(mm::user_stack_bottom(stack_size_bytes));
    this->initial_stack_size_bytes_ = stack_size_bytes;
}

void UserProgram::add_segment(UserSegment segment)
{
    this->ensure_segment_does_not_overlap(segment);
    this->segments_.push_back(std::move(segment));
}

std::span<const UserSegment> UserProgram::segments() const noexcept
{
    return std::span<const UserSegment>{this->segments_};
}

bool UserProgram::entry_point_is_executable() const noexcept
{
    for (const UserSegment& segment : this->segments_)
    {
        if (segment.permissions().executable() && segment.contains(this->entry_point_))
        {
            return true;
        }
    }
    return false;
}

void UserProgram::ensure_segment_does_not_overlap(const UserSegment& segment) const
{
    for (const UserSegment& existing_segment : this->segments_)
    {
        if (ranges_overlap(
                existing_segment.virtual_address(),
                existing_segment.end_address(),
                segment.virtual_address(),
                segment.end_address()))
        {
            throw std::logic_error{USER_PROGRAM_OVERLAP_MESSAGE};
        }
    }
}

UserProcessImage::UserProcessImage(
    const mm::VirtualAddress entry_point,
    const mm::VirtualAddress stack_bottom,
    const mm::VirtualAddress initial_stack_pointer,
    const std::size_t mapped_page_count) noexcept :
    entry_point_(entry_point),
    stack_bottom_(stack_bottom),
    initial_stack_pointer_(initial_stack_pointer),
    mapped_page_count_(mapped_page_count)
{
}

mm::VirtualAddress UserProcessImage::entry_point() const noexcept
{
    return this->entry_point_;
}

mm::VirtualAddress UserProcessImage::stack_bottom() const noexcept
{
    return this->stack_bottom_;
}

mm::VirtualAddress UserProcessImage::initial_stack_pointer() const noexcept
{
    return this->initial_stack_pointer_;
}

std::size_t UserProcessImage::mapped_page_count() const noexcept
{
    return this->mapped_page_count_;
}

UserLoader::UserLoader(mm::PhysicalPageAllocator& allocator, cpu::MemoryBus& memory_bus) noexcept :
    allocator_(&allocator),
    memory_bus_(&memory_bus)
{
}

UserProcessImage UserLoader::load(const UserProgram& program, Process& process)
{
    if (!program.entry_point_is_executable())
    {
        throw std::logic_error{USER_PROGRAM_ENTRY_EXECUTABLE_MESSAGE};
    }

    std::size_t mapped_page_count = std::size_t{0};
    for (const UserSegment& segment : program.segments())
    {
        for (mm::AddressValue offset = mm::AddressValue{0};
             offset < segment.memory_size_bytes();
             offset += mm::MM_PAGE_SIZE_BYTES)
        {
            const mm::PhysicalAddress page = this->allocate_zeroed_page();
            this->copy_segment_page(segment, offset, page);
            process.address_space().map_page(segment.virtual_address() + offset, page, segment.permissions());
            ++mapped_page_count;
        }
    }

    const mm::VirtualAddress stack_bottom = mm::user_stack_bottom(program.initial_stack_size_bytes());
    for (mm::AddressValue offset = mm::AddressValue{0};
         offset < program.initial_stack_size_bytes();
         offset += mm::MM_PAGE_SIZE_BYTES)
    {
        const mm::PhysicalAddress page = this->allocate_zeroed_page();
        process.address_space().map_page(
            stack_bottom + offset,
            page,
            cpu::memory::PagePermissions::user_read_write_no_execute());
        ++mapped_page_count;
    }

    return UserProcessImage{program.entry_point(), stack_bottom, mm::ADDRESS_LAYOUT_USER_STACK_TOP, mapped_page_count};
}

void UserLoader::initialize_user_thread(
    const UserProcessImage& image,
    Process& process,
    sched::ThreadContext& thread) const
{
    thread.reset_cpu_state();
    thread.cpu_state().set_privilege_level(cpu::system::PrivilegeLevel::RING3);
    thread.cpu_state().set_rip(static_cast<cpu::InstructionPointer>(image.entry_point().value()));
    thread.cpu_state().registers().write(
        cpu::RegisterId::RSP,
        static_cast<cpu::Qword>(image.initial_stack_pointer().value()));
    process.address_space().activate(
        thread.cpu_state(),
        UserLoader::process_context_id_for(process.id()),
        cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
}

cpu::memory::ProcessContextId UserLoader::process_context_id_for(const ProcessId process_id)
{
    if (process_id.value() > cpu::memory::PROCESS_CONTEXT_ID_MAX_VALUE)
    {
        throw std::out_of_range{USER_LOADER_PCID_MESSAGE};
    }
    return cpu::memory::ProcessContextId{
        static_cast<cpu::memory::ProcessContextId::value_type>(process_id.value())};
}

void UserLoader::zero_physical_page(const mm::PhysicalAddress physical_address)
{
    for (mm::AddressValue offset = mm::AddressValue{0}; offset < mm::MM_PAGE_SIZE_BYTES; offset += cpu::DATA_SIZE_QWORD_BYTES)
    {
        this->memory_bus_->write(
            mm::to_cpu_address(physical_address + offset),
            cpu::DataSize::QWORD,
            cpu::Qword{0});
    }
}

void UserLoader::copy_segment_page(
    const UserSegment& segment,
    const mm::AddressValue segment_page_offset,
    const mm::PhysicalAddress physical_address)
{
    const std::span<const cpu::Byte> bytes = segment.bytes();
    if (segment_page_offset >= bytes.size())
    {
        return;
    }

    const std::size_t copy_count = std::min(
        static_cast<std::size_t>(mm::MM_PAGE_SIZE_BYTES),
        bytes.size() - static_cast<std::size_t>(segment_page_offset));
    for (std::size_t byte_index = std::size_t{0}; byte_index < copy_count; ++byte_index)
    {
        this->memory_bus_->write(
            mm::to_cpu_address(physical_address + static_cast<mm::AddressValue>(byte_index)),
            cpu::DataSize::BYTE,
            bytes[static_cast<std::size_t>(segment_page_offset) + byte_index]);
    }
}

mm::PhysicalAddress UserLoader::allocate_zeroed_page()
{
    const mm::PhysicalAddress page = this->allocator_->allocate_page();
    this->zero_physical_page(page);
    return page;
}
}
