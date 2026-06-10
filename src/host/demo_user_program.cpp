#include <mnos/host/demo_user_program.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/os/fs/vfs.hpp>
#include <mnos/os/kernel/kernel.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>

namespace
{
constexpr std::size_t HOST_DEMO_ELF64_HEADER_SIZE_BYTES = std::size_t{64};
constexpr std::size_t HOST_DEMO_ELF64_PROGRAM_HEADER_SIZE_BYTES = std::size_t{56};
constexpr std::size_t HOST_DEMO_ELF64_CODE_OFFSET =
    HOST_DEMO_ELF64_HEADER_SIZE_BYTES + HOST_DEMO_ELF64_PROGRAM_HEADER_SIZE_BYTES;
constexpr std::size_t HOST_DEMO_ELF64_EI_MAG0_OFFSET = std::size_t{0};
constexpr std::size_t HOST_DEMO_ELF64_EI_MAG1_OFFSET = std::size_t{1};
constexpr std::size_t HOST_DEMO_ELF64_EI_MAG2_OFFSET = std::size_t{2};
constexpr std::size_t HOST_DEMO_ELF64_EI_MAG3_OFFSET = std::size_t{3};
constexpr std::size_t HOST_DEMO_ELF64_EI_CLASS_OFFSET = std::size_t{4};
constexpr std::size_t HOST_DEMO_ELF64_EI_DATA_OFFSET = std::size_t{5};
constexpr std::size_t HOST_DEMO_ELF64_EI_VERSION_OFFSET = std::size_t{6};
constexpr std::size_t HOST_DEMO_ELF64_E_TYPE_OFFSET = std::size_t{16};
constexpr std::size_t HOST_DEMO_ELF64_E_MACHINE_OFFSET = std::size_t{18};
constexpr std::size_t HOST_DEMO_ELF64_E_VERSION_OFFSET = std::size_t{20};
constexpr std::size_t HOST_DEMO_ELF64_E_ENTRY_OFFSET = std::size_t{24};
constexpr std::size_t HOST_DEMO_ELF64_E_PHOFF_OFFSET = std::size_t{32};
constexpr std::size_t HOST_DEMO_ELF64_E_EHSIZE_OFFSET = std::size_t{52};
constexpr std::size_t HOST_DEMO_ELF64_E_PHENTSIZE_OFFSET = std::size_t{54};
constexpr std::size_t HOST_DEMO_ELF64_E_PHNUM_OFFSET = std::size_t{56};
constexpr std::size_t HOST_DEMO_ELF64_PH_TYPE_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{0};
constexpr std::size_t HOST_DEMO_ELF64_PH_FLAGS_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{4};
constexpr std::size_t HOST_DEMO_ELF64_PH_OFFSET_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{8};
constexpr std::size_t HOST_DEMO_ELF64_PH_VADDR_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{16};
constexpr std::size_t HOST_DEMO_ELF64_PH_FILESZ_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{32};
constexpr std::size_t HOST_DEMO_ELF64_PH_MEMSZ_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{40};
constexpr std::size_t HOST_DEMO_ELF64_PH_ALIGN_OFFSET = HOST_DEMO_ELF64_HEADER_SIZE_BYTES + std::size_t{48};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_MAGIC_0 = mnos::cpu::Byte{0x7F};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_MAGIC_1 = mnos::cpu::Byte{'E'};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_MAGIC_2 = mnos::cpu::Byte{'L'};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_MAGIC_3 = mnos::cpu::Byte{'F'};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_CLASS_64 = mnos::cpu::Byte{2};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_DATA_LITTLE_ENDIAN = mnos::cpu::Byte{1};
constexpr mnos::cpu::Byte HOST_DEMO_ELF64_VERSION_CURRENT_IDENT = mnos::cpu::Byte{1};
constexpr std::uint16_t HOST_DEMO_ELF64_TYPE_EXECUTABLE = std::uint16_t{2};
constexpr std::uint16_t HOST_DEMO_ELF64_MACHINE_X86_64 = std::uint16_t{0x3E};
constexpr std::uint32_t HOST_DEMO_ELF64_VERSION_CURRENT = std::uint32_t{1};
constexpr std::uint32_t HOST_DEMO_ELF64_PROGRAM_TYPE_LOAD = std::uint32_t{1};
constexpr std::uint32_t HOST_DEMO_ELF64_PROGRAM_FLAG_EXECUTE = std::uint32_t{1} << 0;
constexpr std::uint32_t HOST_DEMO_ELF64_PROGRAM_FLAG_READ = std::uint32_t{1} << 2;
constexpr mnos::os::mm::VirtualAddress HOST_DEMO_ELF64_TEXT_BASE =
    mnos::os::mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
constexpr mnos::cpu::Byte HOST_DEMO_X86_REX_W = mnos::cpu::Byte{0x48};
constexpr mnos::cpu::Byte HOST_DEMO_X86_MOV_RAX_IMM64 = mnos::cpu::Byte{0xB8};
constexpr mnos::cpu::Byte HOST_DEMO_X86_MOV_RDI_IMM64 = mnos::cpu::Byte{0xBF};
constexpr mnos::cpu::Byte HOST_DEMO_X86_SYSCALL_ESCAPE = mnos::cpu::Byte{0x0F};
constexpr mnos::cpu::Byte HOST_DEMO_X86_SYSCALL = mnos::cpu::Byte{0x05};
constexpr mnos::cpu::Byte HOST_DEMO_X86_HLT = mnos::cpu::Byte{0xF4};
constexpr const char* HOST_DEMO_BIN_NOT_DIRECTORY_MESSAGE = "host demo /bin path exists but is not a directory";
constexpr const char* HOST_DEMO_EXIT42_NOT_FILE_MESSAGE = "host demo exit42 path exists but is not a file";
constexpr const char* HOST_DEMO_EXIT42_SHORT_WRITE_MESSAGE = "host demo exit42 executable write was truncated";

void write_u16_le(std::vector<mnos::cpu::Byte>& bytes, const std::size_t offset, const std::uint16_t value)
{
    bytes[offset] = static_cast<mnos::cpu::Byte>(value & std::uint16_t{0xFF});
    bytes[offset + std::size_t{1}] = static_cast<mnos::cpu::Byte>((value >> 8U) & std::uint16_t{0xFF});
}

void write_u32_le(std::vector<mnos::cpu::Byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint32_t); ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<mnos::cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS)) & std::uint32_t{0xFF});
    }
}

void write_u64_le(std::vector<mnos::cpu::Byte>& bytes, const std::size_t offset, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<mnos::cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF});
    }
}

void append_u64_le(std::vector<mnos::cpu::Byte>& bytes, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes.push_back(static_cast<mnos::cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * mnos::cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF}));
    }
}

[[nodiscard]] std::vector<mnos::cpu::Byte> make_exit42_code()
{
    std::vector<mnos::cpu::Byte> code;
    code.reserve(std::size_t{24});
    code.push_back(HOST_DEMO_X86_REX_W);
    code.push_back(HOST_DEMO_X86_MOV_RAX_IMM64);
    append_u64_le(code, static_cast<std::uint64_t>(mnos::os::kernel::SyscallNumber::EXIT));
    code.push_back(HOST_DEMO_X86_REX_W);
    code.push_back(HOST_DEMO_X86_MOV_RDI_IMM64);
    append_u64_le(code, static_cast<std::uint64_t>(mnos::host::HOST_DEMO_EXIT42_CODE));
    code.push_back(HOST_DEMO_X86_SYSCALL_ESCAPE);
    code.push_back(HOST_DEMO_X86_SYSCALL);
    code.push_back(HOST_DEMO_X86_HLT);
    return code;
}

void write_elf64_header(std::vector<mnos::cpu::Byte>& bytes)
{
    bytes[HOST_DEMO_ELF64_EI_MAG0_OFFSET] = HOST_DEMO_ELF64_MAGIC_0;
    bytes[HOST_DEMO_ELF64_EI_MAG1_OFFSET] = HOST_DEMO_ELF64_MAGIC_1;
    bytes[HOST_DEMO_ELF64_EI_MAG2_OFFSET] = HOST_DEMO_ELF64_MAGIC_2;
    bytes[HOST_DEMO_ELF64_EI_MAG3_OFFSET] = HOST_DEMO_ELF64_MAGIC_3;
    bytes[HOST_DEMO_ELF64_EI_CLASS_OFFSET] = HOST_DEMO_ELF64_CLASS_64;
    bytes[HOST_DEMO_ELF64_EI_DATA_OFFSET] = HOST_DEMO_ELF64_DATA_LITTLE_ENDIAN;
    bytes[HOST_DEMO_ELF64_EI_VERSION_OFFSET] = HOST_DEMO_ELF64_VERSION_CURRENT_IDENT;
    write_u16_le(bytes, HOST_DEMO_ELF64_E_TYPE_OFFSET, HOST_DEMO_ELF64_TYPE_EXECUTABLE);
    write_u16_le(bytes, HOST_DEMO_ELF64_E_MACHINE_OFFSET, HOST_DEMO_ELF64_MACHINE_X86_64);
    write_u32_le(bytes, HOST_DEMO_ELF64_E_VERSION_OFFSET, HOST_DEMO_ELF64_VERSION_CURRENT);
    write_u64_le(
        bytes,
        HOST_DEMO_ELF64_E_ENTRY_OFFSET,
        HOST_DEMO_ELF64_TEXT_BASE.value() + static_cast<std::uint64_t>(HOST_DEMO_ELF64_CODE_OFFSET));
    write_u64_le(
        bytes,
        HOST_DEMO_ELF64_E_PHOFF_OFFSET,
        static_cast<std::uint64_t>(HOST_DEMO_ELF64_HEADER_SIZE_BYTES));
    write_u16_le(
        bytes,
        HOST_DEMO_ELF64_E_EHSIZE_OFFSET,
        static_cast<std::uint16_t>(HOST_DEMO_ELF64_HEADER_SIZE_BYTES));
    write_u16_le(
        bytes,
        HOST_DEMO_ELF64_E_PHENTSIZE_OFFSET,
        static_cast<std::uint16_t>(HOST_DEMO_ELF64_PROGRAM_HEADER_SIZE_BYTES));
    write_u16_le(bytes, HOST_DEMO_ELF64_E_PHNUM_OFFSET, std::uint16_t{1});
}

void write_elf64_load_program_header(std::vector<mnos::cpu::Byte>& bytes)
{
    write_u32_le(bytes, HOST_DEMO_ELF64_PH_TYPE_OFFSET, HOST_DEMO_ELF64_PROGRAM_TYPE_LOAD);
    write_u32_le(
        bytes,
        HOST_DEMO_ELF64_PH_FLAGS_OFFSET,
        HOST_DEMO_ELF64_PROGRAM_FLAG_READ | HOST_DEMO_ELF64_PROGRAM_FLAG_EXECUTE);
    write_u64_le(bytes, HOST_DEMO_ELF64_PH_OFFSET_OFFSET, std::uint64_t{0});
    write_u64_le(bytes, HOST_DEMO_ELF64_PH_VADDR_OFFSET, HOST_DEMO_ELF64_TEXT_BASE.value());
    write_u64_le(bytes, HOST_DEMO_ELF64_PH_FILESZ_OFFSET, static_cast<std::uint64_t>(bytes.size()));
    write_u64_le(bytes, HOST_DEMO_ELF64_PH_MEMSZ_OFFSET, static_cast<std::uint64_t>(bytes.size()));
    write_u64_le(
        bytes,
        HOST_DEMO_ELF64_PH_ALIGN_OFFSET,
        static_cast<std::uint64_t>(mnos::os::mm::MM_PAGE_SIZE_BYTES));
}

void ensure_demo_bin_directory(mnos::os::fs::Vfs& vfs)
{
    const std::optional<mnos::os::fs::VfsNode> bin_node = vfs.lookup(mnos::host::HOST_DEMO_BIN_DIRECTORY);
    if (!bin_node.has_value())
    {
        static_cast<void>(vfs.create_directory(mnos::host::HOST_DEMO_BIN_DIRECTORY));
        return;
    }
    if (!bin_node->is_directory())
    {
        throw std::invalid_argument{HOST_DEMO_BIN_NOT_DIRECTORY_MESSAGE};
    }
}

void install_file_if_missing(
    mnos::os::fs::Vfs& vfs,
    const std::string_view path,
    const std::span<const mnos::cpu::Byte> bytes)
{
    const std::optional<mnos::os::fs::VfsNode> node = vfs.lookup(path);
    if (node.has_value())
    {
        if (!node->is_file())
        {
            throw std::invalid_argument{HOST_DEMO_EXIT42_NOT_FILE_MESSAGE};
        }
    }
    else
    {
        static_cast<void>(vfs.create_file(path));
    }
    mnos::os::fs::VfsFile file = vfs.open_file(path, mnos::os::fs::VfsOpenMode::READ_WRITE);
    const std::size_t written_byte_count = file.write(bytes);
    if (written_byte_count != bytes.size())
    {
        throw std::runtime_error{HOST_DEMO_EXIT42_SHORT_WRITE_MESSAGE};
    }
}
}

namespace mnos::host
{
std::vector<cpu::Byte> make_host_demo_exit42_elf64()
{
    std::vector<cpu::Byte> code = make_exit42_code();
    std::vector<cpu::Byte> bytes(HOST_DEMO_ELF64_CODE_OFFSET, cpu::Byte{0});
    bytes.insert(bytes.end(), code.begin(), code.end());
    write_elf64_header(bytes);
    write_elf64_load_program_header(bytes);
    return bytes;
}

void install_host_demo_user_programs(os::kernel::Kernel& os_kernel)
{
    os::fs::Vfs& vfs = os_kernel.vfs();
    ensure_demo_bin_directory(vfs);
    const std::vector<cpu::Byte> exit42 = make_host_demo_exit42_elf64();
    install_file_if_missing(vfs, HOST_DEMO_EXIT42_PATH, std::span<const cpu::Byte>{exit42});
}
}
