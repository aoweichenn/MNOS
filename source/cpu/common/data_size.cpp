//
// Created by aoweichen on 2026/6/5.
//

#include <array>
#include <stdexcept>
#include <mnos/cpu/common/data_size.hpp>

namespace
{
constexpr std::string_view DATA_SIZE_ASSEMBLY_NAME_INVALID_TEXT = "<invalid>";
constexpr auto DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE = "data_size invalid size";

constexpr std::array<std::size_t, mnos::DATA_SIZE_KIND_COUNT> DATA_SIZE_BIT_COUNT_TABLE{
    mnos::DATA_SIZE_BYTE_BITS,
    mnos::DATA_SIZE_WORD_BITS,
    mnos::DATA_SIZE_DWORD_BITS,
    mnos::DATA_SIZE_QWORD_BITS
};

constexpr std::array<std::size_t, mnos::DATA_SIZE_KIND_COUNT> DATA_SIZE_BYTE_COUNT_TABLE{
    mnos::DATA_SIZE_BYTE_BYTES,
    mnos::DATA_SIZE_WORD_BYTES,
    mnos::DATA_SIZE_DWORD_BYTES,
    mnos::DATA_SIZE_QWORD_BYTES
};

constexpr std::array<std::string_view, mnos::DATA_SIZE_KIND_COUNT> DATA_SIZE_ASSEMBLY_NAME_TABLE{
    "BYTE",
    "WORD",
    "DWORD",
    "QWORD"
};

[[nodiscard]] constexpr std::size_t DATA_SIZE_TO_INDEX(mnos::DataSize size) noexcept
{
    return static_cast<std::size_t>(size);
}
}

namespace mnos
{
bool is_datat_size_valid(const DataSize size) noexcept
{
    return DATA_SIZE_TO_INDEX(size) < DATA_SIZE_KIND_COUNT;
}

std::size_t data_size_to_bits(const DataSize size)
{
    if (!is_datat_size_valid(size))
    {
        throw std::out_of_range(DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE);
    }
    return DATA_SIZE_BIT_COUNT_TABLE[DATA_SIZE_TO_INDEX(size)];
}

std::string_view data_size_to_assemble_name(const DataSize size) noexcept
{
    if (!is_datat_size_valid(size))
    {
        return DATA_SIZE_ACCESS_INVALID_SIZE_MESSAGE;
    }
    return DATA_SIZE_ASSEMBLY_NAME_TABLE[DATA_SIZE_TO_INDEX(size)];
}
}
