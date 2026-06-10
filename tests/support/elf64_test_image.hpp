#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <mnos/cpu/common/data_size.hpp>
#include <mnos/cpu/common/types.hpp>
#include <mnos/os/kernel/syscall.hpp>
#include <mnos/os/mm/address_layout.hpp>
#include <mnos/os/mm/page.hpp>

namespace mnos::tests::support
{
inline constexpr std::size_t TEST_ELF64_HEADER_SIZE_BYTES = std::size_t{64};
inline constexpr std::size_t TEST_ELF64_PROGRAM_HEADER_SIZE_BYTES = std::size_t{56};
inline constexpr std::size_t TEST_ELF64_EI_MAG0_OFFSET = std::size_t{0};
inline constexpr std::size_t TEST_ELF64_EI_MAG1_OFFSET = std::size_t{1};
inline constexpr std::size_t TEST_ELF64_EI_MAG2_OFFSET = std::size_t{2};
inline constexpr std::size_t TEST_ELF64_EI_MAG3_OFFSET = std::size_t{3};
inline constexpr std::size_t TEST_ELF64_EI_CLASS_OFFSET = std::size_t{4};
inline constexpr std::size_t TEST_ELF64_EI_DATA_OFFSET = std::size_t{5};
inline constexpr std::size_t TEST_ELF64_EI_VERSION_OFFSET = std::size_t{6};
inline constexpr std::size_t TEST_ELF64_E_TYPE_OFFSET = std::size_t{16};
inline constexpr std::size_t TEST_ELF64_E_MACHINE_OFFSET = std::size_t{18};
inline constexpr std::size_t TEST_ELF64_E_VERSION_OFFSET = std::size_t{20};
inline constexpr std::size_t TEST_ELF64_E_ENTRY_OFFSET = std::size_t{24};
inline constexpr std::size_t TEST_ELF64_E_PHOFF_OFFSET = std::size_t{32};
inline constexpr std::size_t TEST_ELF64_E_EHSIZE_OFFSET = std::size_t{52};
inline constexpr std::size_t TEST_ELF64_E_PHENTSIZE_OFFSET = std::size_t{54};
inline constexpr std::size_t TEST_ELF64_E_PHNUM_OFFSET = std::size_t{56};
inline constexpr std::size_t TEST_ELF64_PH_TYPE_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{0};
inline constexpr std::size_t TEST_ELF64_PH_FLAGS_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{4};
inline constexpr std::size_t TEST_ELF64_PH_OFFSET_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{8};
inline constexpr std::size_t TEST_ELF64_PH_VADDR_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{16};
inline constexpr std::size_t TEST_ELF64_PH_FILESZ_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{32};
inline constexpr std::size_t TEST_ELF64_PH_MEMSZ_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{40};
inline constexpr std::size_t TEST_ELF64_PH_ALIGN_OFFSET = TEST_ELF64_HEADER_SIZE_BYTES + std::size_t{48};
inline constexpr std::uint16_t TEST_ELF64_TYPE_EXECUTABLE = std::uint16_t{2};
inline constexpr std::uint16_t TEST_ELF64_MACHINE_X86_64 = std::uint16_t{0x3E};
inline constexpr std::uint32_t TEST_ELF64_PROGRAM_TYPE_LOAD = std::uint32_t{1};
inline constexpr std::uint32_t TEST_ELF64_PROGRAM_FLAG_EXECUTE = std::uint32_t{1} << 0;
inline constexpr std::uint32_t TEST_ELF64_PROGRAM_FLAG_WRITE = std::uint32_t{1} << 1;
inline constexpr std::uint32_t TEST_ELF64_PROGRAM_FLAG_READ = std::uint32_t{1} << 2;
inline constexpr os::mm::VirtualAddress TEST_ELF64_TEXT_BASE = os::mm::ADDRESS_LAYOUT_USER_TEXT_BASE;
inline constexpr os::mm::VirtualAddress TEST_ELF64_DATA_BASE{
    os::mm::ADDRESS_LAYOUT_USER_TEXT_BASE.value() + os::mm::MM_PAGE_SIZE_BYTES};
inline constexpr std::int64_t TEST_ELF64_DEFAULT_EXIT_CODE = std::int64_t{42};

inline void test_elf64_write_u16_le(
    std::vector<cpu::Byte>& bytes,
    const std::size_t offset,
    const std::uint16_t value)
{
    bytes[offset] = static_cast<cpu::Byte>(value & std::uint16_t{0xFF});
    bytes[offset + std::size_t{1}] = static_cast<cpu::Byte>((value >> 8U) & std::uint16_t{0xFF});
}

inline void test_elf64_write_u32_le(
    std::vector<cpu::Byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint32_t); ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) & std::uint32_t{0xFF});
    }
}

inline void test_elf64_write_u64_le(
    std::vector<cpu::Byte>& bytes,
    const std::size_t offset,
    const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes[offset + byte_index] = static_cast<cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF});
    }
}

inline void test_elf64_append_u64_le(std::vector<cpu::Byte>& bytes, const std::uint64_t value)
{
    for (std::size_t byte_index = std::size_t{0}; byte_index < sizeof(std::uint64_t); ++byte_index)
    {
        bytes.push_back(static_cast<cpu::Byte>(
            (value >> static_cast<unsigned>(byte_index * cpu::DATA_SIZE_BYTE_BITS)) & std::uint64_t{0xFF}));
    }
}

[[nodiscard]] inline std::vector<cpu::Byte> test_elf64_exit_program_code(const std::int64_t exit_code)
{
    constexpr cpu::Byte TEST_X86_REX_W = cpu::Byte{0x48};
    constexpr cpu::Byte TEST_X86_MOV_RAX_IMM64 = cpu::Byte{0xB8};
    constexpr cpu::Byte TEST_X86_MOV_RDI_IMM64 = cpu::Byte{0xBF};
    constexpr cpu::Byte TEST_X86_SYSCALL_ESCAPE = cpu::Byte{0x0F};
    constexpr cpu::Byte TEST_X86_SYSCALL = cpu::Byte{0x05};
    constexpr cpu::Byte TEST_X86_HLT = cpu::Byte{0xF4};

    std::vector<cpu::Byte> code;
    code.reserve(std::size_t{24});
    code.push_back(TEST_X86_REX_W);
    code.push_back(TEST_X86_MOV_RAX_IMM64);
    test_elf64_append_u64_le(code, static_cast<std::uint64_t>(os::kernel::SyscallNumber::EXIT));
    code.push_back(TEST_X86_REX_W);
    code.push_back(TEST_X86_MOV_RDI_IMM64);
    test_elf64_append_u64_le(code, static_cast<std::uint64_t>(exit_code));
    code.push_back(TEST_X86_SYSCALL_ESCAPE);
    code.push_back(TEST_X86_SYSCALL);
    code.push_back(TEST_X86_HLT);
    return code;
}

inline void test_elf64_write_header(
    std::vector<cpu::Byte>& bytes,
    const std::size_t program_header_count,
    const os::mm::VirtualAddress entry_point)
{
    bytes[TEST_ELF64_EI_MAG0_OFFSET] = cpu::Byte{0x7F};
    bytes[TEST_ELF64_EI_MAG1_OFFSET] = cpu::Byte{'E'};
    bytes[TEST_ELF64_EI_MAG2_OFFSET] = cpu::Byte{'L'};
    bytes[TEST_ELF64_EI_MAG3_OFFSET] = cpu::Byte{'F'};
    bytes[TEST_ELF64_EI_CLASS_OFFSET] = cpu::Byte{2};
    bytes[TEST_ELF64_EI_DATA_OFFSET] = cpu::Byte{1};
    bytes[TEST_ELF64_EI_VERSION_OFFSET] = cpu::Byte{1};
    test_elf64_write_u16_le(bytes, TEST_ELF64_E_TYPE_OFFSET, TEST_ELF64_TYPE_EXECUTABLE);
    test_elf64_write_u16_le(bytes, TEST_ELF64_E_MACHINE_OFFSET, TEST_ELF64_MACHINE_X86_64);
    test_elf64_write_u32_le(bytes, TEST_ELF64_E_VERSION_OFFSET, std::uint32_t{1});
    test_elf64_write_u64_le(bytes, TEST_ELF64_E_ENTRY_OFFSET, entry_point.value());
    test_elf64_write_u64_le(
        bytes,
        TEST_ELF64_E_PHOFF_OFFSET,
        static_cast<std::uint64_t>(TEST_ELF64_HEADER_SIZE_BYTES));
    test_elf64_write_u16_le(
        bytes,
        TEST_ELF64_E_EHSIZE_OFFSET,
        static_cast<std::uint16_t>(TEST_ELF64_HEADER_SIZE_BYTES));
    test_elf64_write_u16_le(
        bytes,
        TEST_ELF64_E_PHENTSIZE_OFFSET,
        static_cast<std::uint16_t>(TEST_ELF64_PROGRAM_HEADER_SIZE_BYTES));
    test_elf64_write_u16_le(bytes, TEST_ELF64_E_PHNUM_OFFSET, static_cast<std::uint16_t>(program_header_count));
}

inline void test_elf64_write_program_header(
    std::vector<cpu::Byte>& bytes,
    const std::size_t index,
    const std::uint32_t flags,
    const std::uint64_t file_offset,
    const os::mm::VirtualAddress virtual_address,
    const std::uint64_t file_size,
    const std::uint64_t memory_size)
{
    const std::size_t offset =
        TEST_ELF64_HEADER_SIZE_BYTES + (index * TEST_ELF64_PROGRAM_HEADER_SIZE_BYTES);
    test_elf64_write_u32_le(bytes, offset + std::size_t{0}, TEST_ELF64_PROGRAM_TYPE_LOAD);
    test_elf64_write_u32_le(bytes, offset + std::size_t{4}, flags);
    test_elf64_write_u64_le(bytes, offset + std::size_t{8}, file_offset);
    test_elf64_write_u64_le(bytes, offset + std::size_t{16}, virtual_address.value());
    test_elf64_write_u64_le(bytes, offset + std::size_t{32}, file_size);
    test_elf64_write_u64_le(bytes, offset + std::size_t{40}, memory_size);
    test_elf64_write_u64_le(bytes, offset + std::size_t{48}, static_cast<std::uint64_t>(os::mm::MM_PAGE_SIZE_BYTES));
}

[[nodiscard]] inline std::vector<cpu::Byte> make_test_exit_elf64(
    const std::int64_t exit_code = TEST_ELF64_DEFAULT_EXIT_CODE)
{
    constexpr std::size_t TEST_PROGRAM_HEADER_COUNT = std::size_t{1};
    constexpr std::size_t TEST_CODE_OFFSET =
        TEST_ELF64_HEADER_SIZE_BYTES + (TEST_PROGRAM_HEADER_COUNT * TEST_ELF64_PROGRAM_HEADER_SIZE_BYTES);

    std::vector<cpu::Byte> code = test_elf64_exit_program_code(exit_code);
    std::vector<cpu::Byte> bytes(TEST_CODE_OFFSET, cpu::Byte{0});
    bytes.insert(bytes.end(), code.begin(), code.end());
    const os::mm::VirtualAddress entry_point{
        TEST_ELF64_TEXT_BASE.value() + static_cast<os::mm::AddressValue>(TEST_CODE_OFFSET)};
    test_elf64_write_header(bytes, TEST_PROGRAM_HEADER_COUNT, entry_point);
    test_elf64_write_program_header(
        bytes,
        std::size_t{0},
        TEST_ELF64_PROGRAM_FLAG_READ | TEST_ELF64_PROGRAM_FLAG_EXECUTE,
        std::uint64_t{0},
        TEST_ELF64_TEXT_BASE,
        static_cast<std::uint64_t>(bytes.size()),
        static_cast<std::uint64_t>(bytes.size()));
    return bytes;
}

[[nodiscard]] inline std::vector<cpu::Byte> make_test_exit_elf64_with_data_bss(
    const std::int64_t exit_code = TEST_ELF64_DEFAULT_EXIT_CODE)
{
    constexpr std::size_t TEST_PROGRAM_HEADER_COUNT = std::size_t{2};
    constexpr std::size_t TEST_CODE_OFFSET =
        TEST_ELF64_HEADER_SIZE_BYTES + (TEST_PROGRAM_HEADER_COUNT * TEST_ELF64_PROGRAM_HEADER_SIZE_BYTES);
    constexpr std::uint64_t TEST_DATA_MEMORY_SIZE_BYTES = std::uint64_t{16};

    std::vector<cpu::Byte> code = test_elf64_exit_program_code(exit_code);
    std::vector<cpu::Byte> bytes(TEST_CODE_OFFSET, cpu::Byte{0});
    bytes.insert(bytes.end(), code.begin(), code.end());
    const std::uint64_t data_offset = static_cast<std::uint64_t>(bytes.size());
    bytes.push_back(cpu::Byte{0xAA});
    bytes.push_back(cpu::Byte{0xBB});
    bytes.push_back(cpu::Byte{0xCC});
    bytes.push_back(cpu::Byte{0xDD});

    const os::mm::VirtualAddress entry_point{
        TEST_ELF64_TEXT_BASE.value() + static_cast<os::mm::AddressValue>(TEST_CODE_OFFSET)};
    test_elf64_write_header(bytes, TEST_PROGRAM_HEADER_COUNT, entry_point);
    test_elf64_write_program_header(
        bytes,
        std::size_t{0},
        TEST_ELF64_PROGRAM_FLAG_READ | TEST_ELF64_PROGRAM_FLAG_EXECUTE,
        std::uint64_t{0},
        TEST_ELF64_TEXT_BASE,
        data_offset,
        data_offset);
    test_elf64_write_program_header(
        bytes,
        std::size_t{1},
        TEST_ELF64_PROGRAM_FLAG_READ | TEST_ELF64_PROGRAM_FLAG_WRITE,
        data_offset,
        TEST_ELF64_DATA_BASE,
        static_cast<std::uint64_t>(bytes.size()) - data_offset,
        TEST_DATA_MEMORY_SIZE_BYTES);
    return bytes;
}
}
