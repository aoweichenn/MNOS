#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/common/data_size.hpp>

namespace
{
constexpr std::string_view DATA_SIZE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE = "data size is invalid";

constexpr auto DATA_SIZE_BIT_COUNT_TABLE = mnos::make_enum_map<mnos::DataSize>(
    std::array<std::size_t, mnos::DATA_SIZE_KIND_COUNT>{
        mnos::DATA_SIZE_BYTE_BITS,
        mnos::DATA_SIZE_WORD_BITS,
        mnos::DATA_SIZE_DWORD_BITS,
        mnos::DATA_SIZE_QWORD_BITS});

constexpr auto DATA_SIZE_BYTE_COUNT_TABLE = mnos::make_enum_map<mnos::DataSize>(
    std::array<std::size_t, mnos::DATA_SIZE_KIND_COUNT>{
        mnos::DATA_SIZE_BYTE_BYTES,
        mnos::DATA_SIZE_WORD_BYTES,
        mnos::DATA_SIZE_DWORD_BYTES,
        mnos::DATA_SIZE_QWORD_BYTES});

constexpr auto DATA_SIZE_ASSEMBLY_NAME_TABLE = mnos::make_enum_map<mnos::DataSize>(
    std::array<std::string_view, mnos::DATA_SIZE_KIND_COUNT>{"BYTE", "WORD", "DWORD", "QWORD"});
}

namespace mnos
{
bool is_data_size_valid(const DataSize size) noexcept
{
    return DATA_SIZE_BIT_COUNT_TABLE.contains(size);
}

std::size_t data_size_to_bits(const DataSize size)
{
    return DATA_SIZE_BIT_COUNT_TABLE.at(size, DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE);
}

std::size_t data_size_to_bytes(const DataSize size)
{
    return DATA_SIZE_BYTE_COUNT_TABLE.at(size, DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE);
}

std::string_view data_size_to_assembly_name(const DataSize size) noexcept
{
    return DATA_SIZE_ASSEMBLY_NAME_TABLE.value_or(size, DATA_SIZE_ASSEMBLY_NAME_INVALID_TEXT);
}
}
