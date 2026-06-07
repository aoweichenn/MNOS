#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include <mnos/cpu/common/data_size.hpp>
#include <support/test_assert.hpp>

namespace cpu = mnos::cpu;
namespace test = mnos::test;

namespace
{
constexpr auto TEST_INVALID_DATA_SIZE = static_cast<cpu::DataSize>(cpu::DATA_SIZE_KIND_COUNT);

void test_data_size_catalog()
{
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

    for (const DataSizeCase test_case : DATA_SIZE_CASES)
    {
        test::check(cpu::is_data_size_valid(test_case.size), "data size should be valid");
        test::check(cpu::data_size_to_bits(test_case.size) == test_case.bits, "data size bits mismatch");
        test::check(cpu::data_size_to_bytes(test_case.size) == test_case.bytes, "data size bytes mismatch");
        test::check(cpu::data_size_to_assembly_name(test_case.size) == test_case.assembly_name,
                    "data size assembly name mismatch");
    }
}

void test_invalid_data_size()
{
    test::check(!cpu::is_data_size_valid(TEST_INVALID_DATA_SIZE), "invalid data size should be rejected");
    test::check(cpu::data_size_to_assembly_name(TEST_INVALID_DATA_SIZE) == "<invalid>",
                "invalid data size name should be stable");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::data_size_to_bits(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bits access");
    test::check_throws<std::out_of_range>(
        []() {
            static_cast<void>(cpu::data_size_to_bytes(TEST_INVALID_DATA_SIZE));
        },
        "invalid data size bytes access");
}
}

int main()
{
    test_data_size_catalog();
    test_invalid_data_size();

    std::cout << "mnos_cpu_common_unit_tests passed\n";
    return 0;
}
