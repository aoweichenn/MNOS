#include <array>
#include <stdexcept>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <mnos/cpu/common/data_size.hpp>

namespace cpu = mnos::cpu;

namespace
{
using ::testing::Eq;

constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);

struct DataSizeCase
{
    cpu::DataSize size;
    std::size_t bits;
    std::size_t bytes;
    std::string_view assembly_name;
};

constexpr std::array<DataSizeCase, cpu::DATA_SIZE_KIND_COUNT> DATA_SIZE_CASES{
    DataSizeCase{cpu::DataSize::BYTE, cpu::DATA_SIZE_BYTE_BITS, cpu::DATA_SIZE_BYTE_BYTES, "BYTE"},
    DataSizeCase{cpu::DataSize::WORD, cpu::DATA_SIZE_WORD_BITS, cpu::DATA_SIZE_WORD_BYTES, "WORD"},
    DataSizeCase{cpu::DataSize::DWORD, cpu::DATA_SIZE_DWORD_BITS, cpu::DATA_SIZE_DWORD_BYTES, "DWORD"},
    DataSizeCase{cpu::DataSize::QWORD, cpu::DATA_SIZE_QWORD_BITS, cpu::DATA_SIZE_QWORD_BYTES, "QWORD"}};
}

TEST(DataSizeTest, CatalogMapsSizeToBitsBytesAndNames)
{
    for (const DataSizeCase test_case : DATA_SIZE_CASES)
    {
        EXPECT_TRUE(cpu::is_data_size_valid(test_case.size));
        EXPECT_THAT(cpu::data_size_to_bits(test_case.size), Eq(test_case.bits));
        EXPECT_THAT(cpu::data_size_to_bytes(test_case.size), Eq(test_case.bytes));
        EXPECT_THAT(cpu::data_size_to_assembly_name(test_case.size), Eq(test_case.assembly_name));
    }
}

TEST(DataSizeTest, InvalidSizeHasStableQueryBehavior)
{
    EXPECT_FALSE(cpu::is_data_size_valid(TEST_INVALID_DATA_SIZE));
    EXPECT_THAT(cpu::data_size_to_assembly_name(TEST_INVALID_DATA_SIZE), Eq(std::string_view{"<invalid>"}));
    EXPECT_THROW(static_cast<void>(cpu::data_size_to_bits(TEST_INVALID_DATA_SIZE)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(cpu::data_size_to_bytes(TEST_INVALID_DATA_SIZE)), std::out_of_range);
}
