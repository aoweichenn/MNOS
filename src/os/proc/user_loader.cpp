#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/memory/paging.hpp>
#include <mnos/cpu/register/id.hpp>
#include <mnos/cpu/system/privilege.hpp>
#include <mnos/os/proc/process_context.hpp>
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
constexpr const char* USER_PROGRAM_ARGUMENT_NULL_MESSAGE = "user program argument must not contain a null byte";
constexpr const char* USER_PROGRAM_ENVIRONMENT_NULL_MESSAGE = "user program environment must not contain a null byte";
constexpr const char* USER_PROGRAM_STACK_ARGUMENTS_MESSAGE = "user program arguments do not fit in the initial stack";
constexpr std::size_t USER_STACK_QWORD_SIZE_BYTES = sizeof(std::uint64_t);
constexpr mnos::os::mm::AddressValue USER_STACK_ALIGNMENT_BYTES = mnos::os::mm::AddressValue{16};
constexpr mnos::cpu::Qword USER_STACK_NULL_POINTER = mnos::cpu::Qword{0};

struct UserStackStringPointers final
{
    std::vector<mnos::os::mm::VirtualAddress> arguments;
    std::vector<mnos::os::mm::VirtualAddress> environment;
};

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

[[nodiscard]] mnos::os::mm::AddressValue align_down_to_user_stack_alignment(
    const mnos::os::mm::AddressValue value) noexcept
{
    return value - (value % USER_STACK_ALIGNMENT_BYTES);
}

[[nodiscard]] bool stack_subtract(
    const mnos::os::mm::AddressValue stack_bottom,
    const mnos::os::mm::AddressValue cursor,
    const mnos::os::mm::AddressValue byte_count,
    mnos::os::mm::AddressValue& result) noexcept
{
    if (cursor < stack_bottom || byte_count > cursor - stack_bottom)
    {
        return false;
    }
    result = cursor - byte_count;
    return true;
}

[[nodiscard]] mnos::os::mm::AddressValue pointer_table_byte_count(
    const std::size_t argument_count,
    const std::size_t environment_count)
{
    constexpr std::size_t USER_STACK_FIXED_POINTER_SLOT_COUNT = std::size_t{3};
    if (argument_count > std::numeric_limits<std::size_t>::max() - environment_count)
    {
        throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
    }

    const std::size_t dynamic_pointer_slot_count = argument_count + environment_count;
    if (dynamic_pointer_slot_count > std::numeric_limits<std::size_t>::max() - USER_STACK_FIXED_POINTER_SLOT_COUNT)
    {
        throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
    }

    const std::size_t pointer_slot_count = dynamic_pointer_slot_count + USER_STACK_FIXED_POINTER_SLOT_COUNT;
    if (pointer_slot_count > std::numeric_limits<std::size_t>::max() / USER_STACK_QWORD_SIZE_BYTES)
    {
        throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
    }
    return static_cast<mnos::os::mm::AddressValue>(pointer_slot_count * USER_STACK_QWORD_SIZE_BYTES);
}

void write_user_byte(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::os::mm::VirtualAddress virtual_address,
    const mnos::cpu::Byte value)
{
    const mnos::cpu::memory::PageTranslation translation = process.address_space().page_translation(
        virtual_address,
        mnos::cpu::memory::MemoryAccessKind::WRITE,
        mnos::cpu::system::PrivilegeLevel::RING3);
    memory_bus.write(
        translation.translate(mnos::os::mm::to_cpu_address(virtual_address)),
        mnos::cpu::DataSize::BYTE,
        value);
}

void write_user_qword(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::os::mm::VirtualAddress virtual_address,
    const mnos::cpu::Qword value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < USER_STACK_QWORD_SIZE_BYTES; ++byte_index)
    {
        const mnos::cpu::Byte byte_value = static_cast<mnos::cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS)) &
            mnos::cpu::Qword{0xFF});
        write_user_byte(
            process,
            memory_bus,
            virtual_address + static_cast<mnos::os::mm::AddressValue>(byte_index),
            byte_value);
    }
}

void write_user_c_string(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::os::mm::VirtualAddress virtual_address,
    const std::string& text)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < text.size(); ++byte_index)
    {
        write_user_byte(
            process,
            memory_bus,
            virtual_address + static_cast<mnos::os::mm::AddressValue>(byte_index),
            static_cast<mnos::cpu::Byte>(static_cast<unsigned char>(text[byte_index])));
    }
    write_user_byte(
        process,
        memory_bus,
        virtual_address + static_cast<mnos::os::mm::AddressValue>(text.size()),
        mnos::cpu::Byte{0});
}

[[nodiscard]] std::vector<mnos::os::mm::VirtualAddress> write_user_strings(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    const std::span<const std::string> entries,
    const mnos::os::mm::AddressValue stack_bottom,
    mnos::os::mm::AddressValue& cursor)
{
    std::vector<mnos::os::mm::VirtualAddress> pointers(entries.size());
    for (std::size_t reverse_index = entries.size(); reverse_index > std::size_t{0}; --reverse_index)
    {
        const std::size_t entry_index = reverse_index - std::size_t{1};
        const std::string& entry = entries[entry_index];
        if (entry.size() == std::numeric_limits<std::size_t>::max())
        {
            throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
        }
        const mnos::os::mm::AddressValue byte_count =
            static_cast<mnos::os::mm::AddressValue>(entry.size() + std::size_t{1});
        mnos::os::mm::AddressValue string_address_value = mnos::os::mm::AddressValue{0};
        if (!stack_subtract(stack_bottom, cursor, byte_count, string_address_value))
        {
            throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
        }

        const mnos::os::mm::VirtualAddress string_address{string_address_value};
        write_user_c_string(process, memory_bus, string_address, entry);
        pointers[entry_index] = string_address;
        cursor = string_address_value;
    }
    return pointers;
}

void write_pointer_table_entry(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    mnos::os::mm::AddressValue& cursor,
    const mnos::cpu::Qword value)
{
    write_user_qword(process, memory_bus, mnos::os::mm::VirtualAddress{cursor}, value);
    cursor += static_cast<mnos::os::mm::AddressValue>(USER_STACK_QWORD_SIZE_BYTES);
}

void write_pointer_array(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    mnos::os::mm::AddressValue& cursor,
    const std::span<const mnos::os::mm::VirtualAddress> pointers)
{
    for (const mnos::os::mm::VirtualAddress pointer : pointers)
    {
        write_pointer_table_entry(process, memory_bus, cursor, static_cast<mnos::cpu::Qword>(pointer.value()));
    }
}

[[nodiscard]] mnos::os::mm::VirtualAddress write_initial_user_stack(
    mnos::os::proc::Process& process,
    mnos::cpu::MemoryBus& memory_bus,
    const mnos::os::mm::VirtualAddress stack_bottom,
    const mnos::os::proc::UserProgramArguments& arguments)
{
    mnos::os::mm::AddressValue string_cursor = mnos::os::mm::ADDRESS_LAYOUT_USER_STACK_TOP.value();
    UserStackStringPointers string_pointers;
    string_pointers.environment =
        write_user_strings(process, memory_bus, arguments.environment(), stack_bottom.value(), string_cursor);
    string_pointers.arguments =
        write_user_strings(process, memory_bus, arguments.arguments(), stack_bottom.value(), string_cursor);

    const mnos::os::mm::AddressValue table_byte_count =
        pointer_table_byte_count(arguments.argument_count(), arguments.environment_count());
    mnos::os::mm::AddressValue raw_frame_start = mnos::os::mm::AddressValue{0};
    if (!stack_subtract(stack_bottom.value(), string_cursor, table_byte_count, raw_frame_start))
    {
        throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
    }

    const mnos::os::mm::AddressValue frame_start = align_down_to_user_stack_alignment(raw_frame_start);
    if (frame_start < stack_bottom.value())
    {
        throw std::length_error{USER_PROGRAM_STACK_ARGUMENTS_MESSAGE};
    }

    mnos::os::mm::AddressValue table_cursor = frame_start;
    write_pointer_table_entry(
        process,
        memory_bus,
        table_cursor,
        static_cast<mnos::cpu::Qword>(arguments.argument_count()));
    write_pointer_array(
        process,
        memory_bus,
        table_cursor,
        std::span<const mnos::os::mm::VirtualAddress>{string_pointers.arguments});
    write_pointer_table_entry(process, memory_bus, table_cursor, USER_STACK_NULL_POINTER);
    write_pointer_array(
        process,
        memory_bus,
        table_cursor,
        std::span<const mnos::os::mm::VirtualAddress>{string_pointers.environment});
    write_pointer_table_entry(process, memory_bus, table_cursor, USER_STACK_NULL_POINTER);
    return mnos::os::mm::VirtualAddress{frame_start};
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

UserProgramArguments::UserProgramArguments(
    std::vector<std::string> arguments,
    std::vector<std::string> environment) :
    arguments_(std::move(arguments)),
    environment_(std::move(environment))
{
    UserProgramArguments::validate_entries(
        std::span<const std::string>{this->arguments_},
        USER_PROGRAM_ARGUMENT_NULL_MESSAGE);
    UserProgramArguments::validate_entries(
        std::span<const std::string>{this->environment_},
        USER_PROGRAM_ENVIRONMENT_NULL_MESSAGE);
}

std::span<const std::string> UserProgramArguments::arguments() const noexcept
{
    return std::span<const std::string>{this->arguments_};
}

std::span<const std::string> UserProgramArguments::environment() const noexcept
{
    return std::span<const std::string>{this->environment_};
}

std::size_t UserProgramArguments::argument_count() const noexcept
{
    return this->arguments_.size();
}

std::size_t UserProgramArguments::environment_count() const noexcept
{
    return this->environment_.size();
}

bool UserProgramArguments::empty() const noexcept
{
    return this->arguments_.empty() && this->environment_.empty();
}

void UserProgramArguments::validate_entries(
    const std::span<const std::string> entries,
    const std::string_view message)
{
    for (const std::string& entry : entries)
    {
        if (entry.find('\0') != std::string::npos)
        {
            throw std::invalid_argument{std::string{message}};
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
    return this->load(program, process, UserProgramArguments{});
}

UserProcessImage UserLoader::load(
    const UserProgram& program,
    Process& process,
    const UserProgramArguments& arguments)
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

    const mm::VirtualAddress initial_stack_pointer =
        write_initial_user_stack(process, *this->memory_bus_, stack_bottom, arguments);
    return UserProcessImage{program.entry_point(), stack_bottom, initial_stack_pointer, mapped_page_count};
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
        process_context_id_for(process.id()),
        cpu::memory::Cr3TlbFlushMode::FLUSH_CURRENT_CONTEXT);
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
