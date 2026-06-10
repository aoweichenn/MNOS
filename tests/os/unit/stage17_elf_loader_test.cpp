#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/os/mm/page.hpp>
#include <mnos/os/proc/elf_loader.hpp>
#include <support/elf64_test_image.hpp>

namespace cpu = mnos::cpu;
namespace mm = mnos::os::mm;
namespace proc = mnos::os::proc;
namespace elf_test = mnos::tests::support;

namespace
{
using ::testing::Eq;

constexpr cpu::Byte TEST_FIRST_X86_REX_W_BYTE = cpu::Byte{0x48};
constexpr std::uint16_t TEST_ELF64_MACHINE_AARCH64 = std::uint16_t{0xB7};
constexpr std::uint64_t TEST_UNBACKED_ENTRY_DELTA = std::uint64_t{8};

[[nodiscard]] proc::LoadedUserExecutable load_elf64(const std::vector<cpu::Byte>& bytes)
{
    return proc::Elf64Loader{}.load(std::span<const cpu::Byte>{bytes});
}
}

TEST(Stage17ElfLoaderTest, LoadsExecutableSegmentAndImageCoversEntryInsideTextSegment)
{
    const std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64();

    const proc::LoadedUserExecutable loaded = load_elf64(bytes);

    EXPECT_TRUE(loaded.program().entry_point_is_executable());
    ASSERT_THAT(loaded.program().segments().size(), Eq(std::size_t{1}));
    const proc::UserSegment& text_segment = loaded.program().segments()[std::size_t{0}];
    EXPECT_THAT(text_segment.virtual_address(), Eq(elf_test::TEST_ELF64_TEXT_BASE));
    EXPECT_TRUE(text_segment.permissions().user_accessible());
    EXPECT_TRUE(text_segment.permissions().executable());
    EXPECT_FALSE(text_segment.permissions().writable());
    EXPECT_THAT(text_segment.memory_size_bytes(), Eq(mm::MM_PAGE_SIZE_BYTES));
    EXPECT_THAT(text_segment.bytes().size(), Eq(bytes.size()));

    EXPECT_THAT(loaded.executable_image().base_rip(), Eq(elf_test::TEST_ELF64_TEXT_BASE.value()));
    EXPECT_TRUE(loaded.executable_image().contains_rip(loaded.program().entry_point().value()));
    EXPECT_THAT(
        loaded.executable_image().byte_at(loaded.program().entry_point().value()),
        Eq(TEST_FIRST_X86_REX_W_BYTE));
}

TEST(Stage17ElfLoaderTest, LoadsWritableDataBssSegmentWithoutExecutePermission)
{
    const std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64_with_data_bss();

    const proc::LoadedUserExecutable loaded = load_elf64(bytes);

    ASSERT_THAT(loaded.program().segments().size(), Eq(std::size_t{2}));
    const proc::UserSegment& text_segment = loaded.program().segments()[std::size_t{0}];
    const proc::UserSegment& data_segment = loaded.program().segments()[std::size_t{1}];
    EXPECT_TRUE(text_segment.permissions().executable());
    EXPECT_THAT(data_segment.virtual_address(), Eq(elf_test::TEST_ELF64_DATA_BASE));
    EXPECT_TRUE(data_segment.permissions().user_accessible());
    EXPECT_TRUE(data_segment.permissions().writable());
    EXPECT_FALSE(data_segment.permissions().executable());
    EXPECT_THAT(data_segment.bytes().size(), Eq(std::size_t{4}));
    EXPECT_THAT(data_segment.memory_size_bytes(), Eq(mm::MM_PAGE_SIZE_BYTES));
}

TEST(Stage17ElfLoaderTest, RejectsInvalidHeaderIdentityAndArchitecture)
{
    std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64();
    bytes[elf_test::TEST_ELF64_EI_MAG0_OFFSET] = cpu::Byte{0};
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    bytes[elf_test::TEST_ELF64_EI_CLASS_OFFSET] = cpu::Byte{1};
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u16_le(
        bytes,
        elf_test::TEST_ELF64_E_MACHINE_OFFSET,
        TEST_ELF64_MACHINE_AARCH64);
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);
}

TEST(Stage17ElfLoaderTest, RejectsMalformedProgramHeaderTableAndLoadSegments)
{
    std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u16_le(bytes, elf_test::TEST_ELF64_E_PHNUM_OFFSET, std::uint16_t{0});
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u16_le(bytes, elf_test::TEST_ELF64_E_PHNUM_OFFSET, std::uint16_t{2});
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u64_le(
        bytes,
        elf_test::TEST_ELF64_PH_FILESZ_OFFSET,
        static_cast<std::uint64_t>(bytes.size()) + std::uint64_t{1});
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u64_le(bytes, elf_test::TEST_ELF64_PH_ALIGN_OFFSET, std::uint64_t{3});
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);
}

TEST(Stage17ElfLoaderTest, RejectsUnsafeOrUnusableExecutableSegments)
{
    std::vector<cpu::Byte> bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u32_le(
        bytes,
        elf_test::TEST_ELF64_PH_FLAGS_OFFSET,
        elf_test::TEST_ELF64_PROGRAM_FLAG_READ |
            elf_test::TEST_ELF64_PROGRAM_FLAG_WRITE |
            elf_test::TEST_ELF64_PROGRAM_FLAG_EXECUTE);
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    elf_test::test_elf64_write_u64_le(
        bytes,
        elf_test::TEST_ELF64_PH_VADDR_OFFSET,
        elf_test::TEST_ELF64_TEXT_BASE.value() + std::uint64_t{1});
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64();
    const std::uint64_t unbacked_entry =
        elf_test::TEST_ELF64_TEXT_BASE.value() +
        static_cast<std::uint64_t>(bytes.size()) +
        TEST_UNBACKED_ENTRY_DELTA;
    elf_test::test_elf64_write_u64_le(
        bytes,
        elf_test::TEST_ELF64_PH_MEMSZ_OFFSET,
        (unbacked_entry - elf_test::TEST_ELF64_TEXT_BASE.value()) + std::uint64_t{1});
    elf_test::test_elf64_write_u64_le(
        bytes,
        elf_test::TEST_ELF64_E_ENTRY_OFFSET,
        unbacked_entry);
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);

    bytes = elf_test::make_test_exit_elf64_with_data_bss();
    elf_test::test_elf64_write_u64_le(
        bytes,
        elf_test::TEST_ELF64_E_ENTRY_OFFSET,
        elf_test::TEST_ELF64_DATA_BASE.value());
    EXPECT_THROW(static_cast<void>(load_elf64(bytes)), std::invalid_argument);
}
