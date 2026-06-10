#include <mnos/os/proc/elf_loader.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>

namespace
{
constexpr std::size_t ELF64_HEADER_SIZE_BYTES = std::size_t{64};
constexpr std::size_t ELF64_PROGRAM_HEADER_SIZE_BYTES = std::size_t{56};
constexpr std::size_t ELF64_EI_MAG0_OFFSET = std::size_t{0};
constexpr std::size_t ELF64_EI_MAG1_OFFSET = std::size_t{1};
constexpr std::size_t ELF64_EI_MAG2_OFFSET = std::size_t{2};
constexpr std::size_t ELF64_EI_MAG3_OFFSET = std::size_t{3};
constexpr std::size_t ELF64_EI_CLASS_OFFSET = std::size_t{4};
constexpr std::size_t ELF64_EI_DATA_OFFSET = std::size_t{5};
constexpr std::size_t ELF64_EI_VERSION_OFFSET = std::size_t{6};
constexpr std::size_t ELF64_E_TYPE_OFFSET = std::size_t{16};
constexpr std::size_t ELF64_E_MACHINE_OFFSET = std::size_t{18};
constexpr std::size_t ELF64_E_VERSION_OFFSET = std::size_t{20};
constexpr std::size_t ELF64_E_ENTRY_OFFSET = std::size_t{24};
constexpr std::size_t ELF64_E_PHOFF_OFFSET = std::size_t{32};
constexpr std::size_t ELF64_E_EHSIZE_OFFSET = std::size_t{52};
constexpr std::size_t ELF64_E_PHENTSIZE_OFFSET = std::size_t{54};
constexpr std::size_t ELF64_E_PHNUM_OFFSET = std::size_t{56};
constexpr std::size_t ELF64_PH_TYPE_OFFSET = std::size_t{0};
constexpr std::size_t ELF64_PH_FLAGS_OFFSET = std::size_t{4};
constexpr std::size_t ELF64_PH_OFFSET_OFFSET = std::size_t{8};
constexpr std::size_t ELF64_PH_VADDR_OFFSET = std::size_t{16};
constexpr std::size_t ELF64_PH_FILESZ_OFFSET = std::size_t{32};
constexpr std::size_t ELF64_PH_MEMSZ_OFFSET = std::size_t{40};
constexpr std::size_t ELF64_PH_ALIGN_OFFSET = std::size_t{48};
constexpr mnos::cpu::Byte ELF64_MAGIC_0 = mnos::cpu::Byte{0x7F};
constexpr mnos::cpu::Byte ELF64_MAGIC_1 = mnos::cpu::Byte{'E'};
constexpr mnos::cpu::Byte ELF64_MAGIC_2 = mnos::cpu::Byte{'L'};
constexpr mnos::cpu::Byte ELF64_MAGIC_3 = mnos::cpu::Byte{'F'};
constexpr mnos::cpu::Byte ELF64_CLASS_64 = mnos::cpu::Byte{2};
constexpr mnos::cpu::Byte ELF64_DATA_LITTLE_ENDIAN = mnos::cpu::Byte{1};
constexpr mnos::cpu::Byte ELF64_IDENT_VERSION_CURRENT = mnos::cpu::Byte{1};
constexpr std::uint16_t ELF64_TYPE_EXECUTABLE = std::uint16_t{2};
constexpr std::uint16_t ELF64_MACHINE_X86_64 = std::uint16_t{0x3E};
constexpr std::uint32_t ELF64_VERSION_CURRENT = std::uint32_t{1};
constexpr std::uint32_t ELF64_PROGRAM_TYPE_LOAD = std::uint32_t{1};
constexpr std::uint32_t ELF64_PROGRAM_FLAG_EXECUTE = std::uint32_t{1} << 0;
constexpr std::uint32_t ELF64_PROGRAM_FLAG_WRITE = std::uint32_t{1} << 1;
constexpr const char* ELF64_SMALL_FILE_MESSAGE = "elf64 file is smaller than the ELF header";
constexpr const char* ELF64_RANGE_MESSAGE = "elf64 file range is outside the input bytes";
constexpr const char* ELF64_MAGIC_MESSAGE = "elf64 file has invalid ELF magic";
constexpr const char* ELF64_CLASS_MESSAGE = "elf64 loader only supports ELFCLASS64";
constexpr const char* ELF64_ENDIAN_MESSAGE = "elf64 loader only supports little-endian files";
constexpr const char* ELF64_VERSION_MESSAGE = "elf64 file version is unsupported";
constexpr const char* ELF64_TYPE_MESSAGE = "elf64 loader only supports executable ET_EXEC files";
constexpr const char* ELF64_MACHINE_MESSAGE = "elf64 loader only supports x86-64 files";
constexpr const char* ELF64_HEADER_SIZE_MESSAGE = "elf64 header size is unsupported";
constexpr const char* ELF64_PROGRAM_HEADER_SIZE_MESSAGE = "elf64 program header size is unsupported";
constexpr const char* ELF64_PROGRAM_HEADER_COUNT_MESSAGE = "elf64 executable must contain program headers";
constexpr const char* ELF64_PROGRAM_HEADER_RANGE_MESSAGE = "elf64 program header table is outside the input bytes";
constexpr const char* ELF64_LOAD_FILE_SIZE_MESSAGE = "elf64 load segment file size exceeds memory size";
constexpr const char* ELF64_LOAD_EMPTY_MESSAGE = "elf64 load segment memory size must not be zero";
constexpr const char* ELF64_LOAD_ALIGNMENT_MESSAGE = "elf64 load segment alignment must be zero or a power of two";
constexpr const char* ELF64_LOAD_ADDRESS_ALIGNMENT_MESSAGE = "elf64 load segment virtual address must be page aligned";
constexpr const char* ELF64_LOAD_RANGE_MESSAGE = "elf64 load segment file bytes are outside the input bytes";
constexpr const char* ELF64_LOAD_USER_RANGE_MESSAGE = "elf64 load segment must live inside user address range";
constexpr const char* ELF64_LOAD_WX_MESSAGE = "elf64 load segment must not be both writable and executable";
constexpr const char* ELF64_NO_LOAD_SEGMENT_MESSAGE = "elf64 executable has no loadable segments";
constexpr const char* ELF64_ENTRY_SEGMENT_MESSAGE = "elf64 entry point must be inside an executable load segment";
constexpr const char* ELF64_ENTRY_FILE_BACKING_MESSAGE =
    "elf64 entry point must be backed by executable file bytes";

struct Elf64Header final
{
    mnos::os::mm::VirtualAddress entry_point;
    std::uint64_t program_header_offset = std::uint64_t{0};
    std::uint16_t program_header_entry_size = std::uint16_t{0};
    std::uint16_t program_header_count = std::uint16_t{0};
};

struct Elf64ProgramHeader final
{
    std::uint32_t type = std::uint32_t{0};
    std::uint32_t flags = std::uint32_t{0};
    std::uint64_t offset = std::uint64_t{0};
    std::uint64_t virtual_address = std::uint64_t{0};
    std::uint64_t file_size = std::uint64_t{0};
    std::uint64_t memory_size = std::uint64_t{0};
    std::uint64_t alignment = std::uint64_t{0};
};

struct Elf64LoadSegment final
{
    mnos::os::proc::UserSegment segment;
    bool executable = false;
};

[[nodiscard]] bool is_power_of_two(const std::uint64_t value) noexcept
{
    return value != std::uint64_t{0} && (value & (value - std::uint64_t{1})) == std::uint64_t{0};
}

[[nodiscard]] std::uint64_t checked_add_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const message)
{
    if (left > std::numeric_limits<std::uint64_t>::max() - right)
    {
        throw std::overflow_error{message};
    }
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply_u64(
    const std::uint64_t left,
    const std::uint64_t right,
    const char* const message)
{
    if (left != std::uint64_t{0} && right > std::numeric_limits<std::uint64_t>::max() / left)
    {
        throw std::overflow_error{message};
    }
    return left * right;
}

void require_file_range(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const std::uint64_t offset,
    const std::uint64_t byte_count,
    const char* const message)
{
    const std::uint64_t file_size = static_cast<std::uint64_t>(file_bytes.size());
    if (offset > file_size || byte_count > file_size - offset)
    {
        throw std::invalid_argument{message};
    }
}

[[nodiscard]] std::size_t size_t_from_u64(const std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::overflow_error{ELF64_RANGE_MESSAGE};
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::span<const mnos::cpu::Byte> file_subspan(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const std::uint64_t offset,
    const std::uint64_t byte_count,
    const char* const message)
{
    require_file_range(file_bytes, offset, byte_count, message);
    return file_bytes.subspan(size_t_from_u64(offset), size_t_from_u64(byte_count));
}

[[nodiscard]] std::uint16_t read_u16_le(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const std::size_t offset)
{
    const std::uint32_t value = static_cast<std::uint32_t>(file_bytes[offset]) |
        (static_cast<std::uint32_t>(file_bytes[offset + std::size_t{1}]) << 8U);
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const std::size_t offset)
{
    std::uint32_t value = std::uint32_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint32_t); ++byte_index)
    {
        value |= static_cast<std::uint32_t>(file_bytes[offset + byte_index]) << static_cast<unsigned>(byte_index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_le(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const std::size_t offset)
{
    std::uint64_t value = std::uint64_t{0};
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        value |= static_cast<std::uint64_t>(file_bytes[offset + byte_index]) << static_cast<unsigned>(byte_index * 8U);
    }
    return value;
}

void validate_elf_ident(const std::span<const mnos::cpu::Byte> file_bytes)
{
    if (file_bytes.size() < ELF64_HEADER_SIZE_BYTES)
    {
        throw std::invalid_argument{ELF64_SMALL_FILE_MESSAGE};
    }
    if (file_bytes[ELF64_EI_MAG0_OFFSET] != ELF64_MAGIC_0 ||
        file_bytes[ELF64_EI_MAG1_OFFSET] != ELF64_MAGIC_1 ||
        file_bytes[ELF64_EI_MAG2_OFFSET] != ELF64_MAGIC_2 ||
        file_bytes[ELF64_EI_MAG3_OFFSET] != ELF64_MAGIC_3)
    {
        throw std::invalid_argument{ELF64_MAGIC_MESSAGE};
    }
    if (file_bytes[ELF64_EI_CLASS_OFFSET] != ELF64_CLASS_64)
    {
        throw std::invalid_argument{ELF64_CLASS_MESSAGE};
    }
    if (file_bytes[ELF64_EI_DATA_OFFSET] != ELF64_DATA_LITTLE_ENDIAN)
    {
        throw std::invalid_argument{ELF64_ENDIAN_MESSAGE};
    }
    if (file_bytes[ELF64_EI_VERSION_OFFSET] != ELF64_IDENT_VERSION_CURRENT)
    {
        throw std::invalid_argument{ELF64_VERSION_MESSAGE};
    }
}

[[nodiscard]] Elf64Header read_elf_header(const std::span<const mnos::cpu::Byte> file_bytes)
{
    validate_elf_ident(file_bytes);

    if (read_u16_le(file_bytes, ELF64_E_TYPE_OFFSET) != ELF64_TYPE_EXECUTABLE)
    {
        throw std::invalid_argument{ELF64_TYPE_MESSAGE};
    }
    if (read_u16_le(file_bytes, ELF64_E_MACHINE_OFFSET) != ELF64_MACHINE_X86_64)
    {
        throw std::invalid_argument{ELF64_MACHINE_MESSAGE};
    }
    if (read_u32_le(file_bytes, ELF64_E_VERSION_OFFSET) != ELF64_VERSION_CURRENT)
    {
        throw std::invalid_argument{ELF64_VERSION_MESSAGE};
    }
    if (read_u16_le(file_bytes, ELF64_E_EHSIZE_OFFSET) != ELF64_HEADER_SIZE_BYTES)
    {
        throw std::invalid_argument{ELF64_HEADER_SIZE_MESSAGE};
    }
    if (read_u16_le(file_bytes, ELF64_E_PHENTSIZE_OFFSET) != ELF64_PROGRAM_HEADER_SIZE_BYTES)
    {
        throw std::invalid_argument{ELF64_PROGRAM_HEADER_SIZE_MESSAGE};
    }

    Elf64Header header;
    header.entry_point = mnos::os::mm::VirtualAddress{read_u64_le(file_bytes, ELF64_E_ENTRY_OFFSET)};
    header.program_header_offset = read_u64_le(file_bytes, ELF64_E_PHOFF_OFFSET);
    header.program_header_entry_size = read_u16_le(file_bytes, ELF64_E_PHENTSIZE_OFFSET);
    header.program_header_count = read_u16_le(file_bytes, ELF64_E_PHNUM_OFFSET);
    if (header.program_header_count == std::uint16_t{0})
    {
        throw std::invalid_argument{ELF64_PROGRAM_HEADER_COUNT_MESSAGE};
    }

    const std::uint64_t program_header_table_bytes = checked_multiply_u64(
        header.program_header_entry_size,
        header.program_header_count,
        ELF64_PROGRAM_HEADER_RANGE_MESSAGE);
    require_file_range(
        file_bytes,
        header.program_header_offset,
        program_header_table_bytes,
        ELF64_PROGRAM_HEADER_RANGE_MESSAGE);
    return header;
}

[[nodiscard]] Elf64ProgramHeader read_program_header(
    const std::span<const mnos::cpu::Byte> program_header_bytes)
{
    Elf64ProgramHeader header;
    header.type = read_u32_le(program_header_bytes, ELF64_PH_TYPE_OFFSET);
    header.flags = read_u32_le(program_header_bytes, ELF64_PH_FLAGS_OFFSET);
    header.offset = read_u64_le(program_header_bytes, ELF64_PH_OFFSET_OFFSET);
    header.virtual_address = read_u64_le(program_header_bytes, ELF64_PH_VADDR_OFFSET);
    header.file_size = read_u64_le(program_header_bytes, ELF64_PH_FILESZ_OFFSET);
    header.memory_size = read_u64_le(program_header_bytes, ELF64_PH_MEMSZ_OFFSET);
    header.alignment = read_u64_le(program_header_bytes, ELF64_PH_ALIGN_OFFSET);
    return header;
}

[[nodiscard]] bool program_header_is_executable(const Elf64ProgramHeader& header) noexcept
{
    return (header.flags & ELF64_PROGRAM_FLAG_EXECUTE) != std::uint32_t{0};
}

[[nodiscard]] bool program_header_is_writable(const Elf64ProgramHeader& header) noexcept
{
    return (header.flags & ELF64_PROGRAM_FLAG_WRITE) != std::uint32_t{0};
}

[[nodiscard]] mnos::cpu::memory::PagePermissions permissions_from_program_header(
    const Elf64ProgramHeader& header)
{
    if (program_header_is_writable(header) && program_header_is_executable(header))
    {
        throw std::invalid_argument{ELF64_LOAD_WX_MESSAGE};
    }
    if (program_header_is_writable(header))
    {
        return mnos::cpu::memory::PagePermissions::user_read_write_no_execute();
    }
    if (program_header_is_executable(header))
    {
        return mnos::cpu::memory::PagePermissions::user_read_only_execute();
    }
    return mnos::cpu::memory::PagePermissions{false, true, false};
}

void validate_load_program_header(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const Elf64ProgramHeader& header)
{
    if (header.memory_size == std::uint64_t{0})
    {
        throw std::invalid_argument{ELF64_LOAD_EMPTY_MESSAGE};
    }
    if (header.file_size > header.memory_size)
    {
        throw std::invalid_argument{ELF64_LOAD_FILE_SIZE_MESSAGE};
    }
    if (header.alignment != std::uint64_t{0} && !is_power_of_two(header.alignment))
    {
        throw std::invalid_argument{ELF64_LOAD_ALIGNMENT_MESSAGE};
    }
    const mnos::os::mm::VirtualAddress virtual_address{header.virtual_address};
    if (!mnos::os::mm::is_page_aligned(virtual_address))
    {
        throw std::invalid_argument{ELF64_LOAD_ADDRESS_ALIGNMENT_MESSAGE};
    }
    require_file_range(file_bytes, header.offset, header.file_size, ELF64_LOAD_RANGE_MESSAGE);
    const mnos::os::mm::AddressValue mapped_size = mnos::os::mm::align_up_value(header.memory_size);
    if (!mnos::os::mm::is_user_range(virtual_address, mapped_size))
    {
        throw std::out_of_range{ELF64_LOAD_USER_RANGE_MESSAGE};
    }
    static_cast<void>(permissions_from_program_header(header));
}

[[nodiscard]] Elf64LoadSegment make_load_segment(
    const std::span<const mnos::cpu::Byte> file_bytes,
    const Elf64ProgramHeader& header)
{
    validate_load_program_header(file_bytes, header);

    const std::span<const mnos::cpu::Byte> segment_file_bytes =
        file_subspan(file_bytes, header.offset, header.file_size, ELF64_LOAD_RANGE_MESSAGE);
    std::vector<mnos::cpu::Byte> segment_bytes{segment_file_bytes.begin(), segment_file_bytes.end()};
    return Elf64LoadSegment{
        mnos::os::proc::UserSegment{
            mnos::os::mm::VirtualAddress{header.virtual_address},
            std::move(segment_bytes),
            mnos::os::mm::align_up_value(header.memory_size),
            permissions_from_program_header(header)},
        program_header_is_executable(header)};
}

[[nodiscard]] std::optional<mnos::cpu::ExecutableImage> executable_image_for_entry(
    const Elf64LoadSegment& load_segment,
    const mnos::os::mm::VirtualAddress entry_point)
{
    if (!load_segment.executable || !load_segment.segment.contains(entry_point))
    {
        return std::nullopt;
    }

    const mnos::os::mm::AddressValue entry_offset = entry_point - load_segment.segment.virtual_address();
    const std::span<const mnos::cpu::Byte> bytes = load_segment.segment.bytes();
    if (entry_offset >= static_cast<mnos::os::mm::AddressValue>(bytes.size()))
    {
        throw std::invalid_argument{ELF64_ENTRY_FILE_BACKING_MESSAGE};
    }
    return mnos::cpu::ExecutableImage{
        mnos::cpu::ExecutableImage::container_type{bytes.begin(), bytes.end()},
        static_cast<mnos::cpu::InstructionPointer>(load_segment.segment.virtual_address().value())};
}
}

namespace mnos::os::proc
{
LoadedUserExecutable::LoadedUserExecutable(UserProgram program, cpu::ExecutableImage executable_image) :
    program_(std::move(program)),
    executable_image_(std::move(executable_image))
{
}

const UserProgram& LoadedUserExecutable::program() const noexcept
{
    return this->program_;
}

const cpu::ExecutableImage& LoadedUserExecutable::executable_image() const noexcept
{
    return this->executable_image_;
}

LoadedUserExecutable Elf64Loader::load(const std::span<const cpu::Byte> file_bytes) const
{
    const Elf64Header header = read_elf_header(file_bytes);
    UserProgram program{header.entry_point};
    std::optional<cpu::ExecutableImage> executable_image;
    std::size_t load_segment_count = std::size_t{0};

    for (std::uint16_t program_header_index = std::uint16_t{0};
         program_header_index < header.program_header_count;
         ++program_header_index)
    {
        const std::uint64_t program_header_offset = checked_add_u64(
            header.program_header_offset,
            checked_multiply_u64(
                program_header_index,
                header.program_header_entry_size,
                ELF64_PROGRAM_HEADER_RANGE_MESSAGE),
            ELF64_PROGRAM_HEADER_RANGE_MESSAGE);
        const std::span<const cpu::Byte> program_header_bytes = file_subspan(
            file_bytes,
            program_header_offset,
            ELF64_PROGRAM_HEADER_SIZE_BYTES,
            ELF64_PROGRAM_HEADER_RANGE_MESSAGE);
        const Elf64ProgramHeader program_header = read_program_header(program_header_bytes);
        if (program_header.type != ELF64_PROGRAM_TYPE_LOAD)
        {
            continue;
        }

        Elf64LoadSegment load_segment = make_load_segment(file_bytes, program_header);
        ++load_segment_count;
        std::optional<cpu::ExecutableImage> entry_image =
            executable_image_for_entry(load_segment, header.entry_point);
        if (entry_image.has_value())
        {
            executable_image = std::move(entry_image.value());
        }
        program.add_segment(std::move(load_segment.segment));
    }

    if (load_segment_count == std::size_t{0})
    {
        throw std::invalid_argument{ELF64_NO_LOAD_SEGMENT_MESSAGE};
    }
    if (!program.entry_point_is_executable() || !executable_image.has_value())
    {
        throw std::invalid_argument{ELF64_ENTRY_SEGMENT_MESSAGE};
    }

    return LoadedUserExecutable{std::move(program), std::move(executable_image.value())};
}
}
