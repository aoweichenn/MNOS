//
// Created by aoweichen on 2026/6/5.
//

#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnos
{
enum class DataSize : std::uint8_t{
    BYTE,
    WORD,
    DWORD,
    QWORD,
    COUNT
};

inline constexpr std::size_t DATA_SIZE_KIND_COUNT = static_cast<std::size_t>(DataSize::COUNT);

// 数据大小的不同表示
inline constexpr std::size_t DATA_SIZE_BYTE_BITS = 8;
inline constexpr std::size_t DATA_SIZE_WORD_BITS = 16;
inline constexpr std::size_t DATA_SIZE_DWORD_BITS = 32;
inline constexpr std::size_t DATA_SIZE_QWORD_BITS = 64;

inline constexpr std::size_t DATA_SIZE_BYTE_BYTES = 1;
inline constexpr std::size_t DATA_SIZE_WORD_BYTES = 2;
inline constexpr std::size_t DATA_SIZE_DWORD_BYTES = 3;
inline constexpr std::size_t DATA_SIZE_QWORD_BYTES = 4;

[[nodiscard]] bool is_datat_size_valid(DataSize size) noexcept;
[[nodiscard]] std::size_t data_size_to_bits(DataSize size);
[[nodiscard]] std::string_view data_size_to_assemble_name(DataSize size) noexcept;
}
