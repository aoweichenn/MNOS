#include <array>
#include <string_view>

#include <mnos/core/enum_map.hpp>
#include <mnos/cpu/common/data_size.hpp>

namespace
{
constexpr std::string_view DATA_SIZE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr const char* DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE = "data size is invalid";

class DataSizeCatalog
{
public:
    [[nodiscard]] static bool contains(const mnos::cpu::DataSize size) noexcept
    {
        return DATA_SIZE_NAMES.contains(size);
    }

    [[nodiscard]] static std::size_t bits(const mnos::cpu::DataSize size)
    {
        return DATA_SIZE_BIT_COUNTS.at(size, DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE);
    }

    [[nodiscard]] static std::size_t bytes(const mnos::cpu::DataSize size)
    {
        return DATA_SIZE_BYTE_COUNTS.at(size, DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE);
    }

    [[nodiscard]] static std::string_view assembly_name(const mnos::cpu::DataSize size) noexcept
    {
        return DATA_SIZE_NAMES.name(size);
    }

private:
    inline static constexpr auto DATA_SIZE_BIT_COUNTS = mnos::core::make_enum_map<mnos::cpu::DataSize>(
        std::array<std::size_t, mnos::cpu::DATA_SIZE_COUNT>{
            mnos::cpu::DATA_SIZE_BYTE_BITS,
            mnos::cpu::DATA_SIZE_WORD_BITS,
            mnos::cpu::DATA_SIZE_DWORD_BITS,
            mnos::cpu::DATA_SIZE_QWORD_BITS});

    inline static constexpr auto DATA_SIZE_BYTE_COUNTS = mnos::core::make_enum_map<mnos::cpu::DataSize>(
        std::array<std::size_t, mnos::cpu::DATA_SIZE_COUNT>{
            mnos::cpu::DATA_SIZE_BYTE_BYTES,
            mnos::cpu::DATA_SIZE_WORD_BYTES,
            mnos::cpu::DATA_SIZE_DWORD_BYTES,
            mnos::cpu::DATA_SIZE_QWORD_BYTES});

    inline static constexpr auto DATA_SIZE_NAMES = mnos::core::make_enum_name_table<mnos::cpu::DataSize>(
        std::array<std::string_view, mnos::cpu::DATA_SIZE_COUNT>{"BYTE", "WORD", "DWORD", "QWORD"},
        DATA_SIZE_ASSEMBLY_NAME_INVALID_TEXT);
};
}

namespace mnos::cpu
{
bool is_data_size_valid(const DataSize size) noexcept
{
    return DataSizeCatalog::contains(size);
}

std::size_t data_size_to_bits(const DataSize size)
{
    return DataSizeCatalog::bits(size);
}

std::size_t data_size_to_bytes(const DataSize size)
{
    return DataSizeCatalog::bytes(size);
}

std::string_view data_size_to_assembly_name(const DataSize size) noexcept
{
    return DataSizeCatalog::assembly_name(size);
}
}
