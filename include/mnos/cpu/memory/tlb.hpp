#pragma once

#include <array>
#include <cstddef>

#include <mnos/cpu/common/types.hpp>
#include <mnos/cpu/memory/paging.hpp>

namespace mnos::cpu::memory
{
inline constexpr std::size_t TRANSLATION_LOOKASIDE_BUFFER_ENTRY_COUNT = 64;

class TranslationLookasideBuffer final
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    void clear() noexcept;
    void invalidate_page(Address64 linear_address) noexcept;

    [[nodiscard]] PageTranslation* lookup(Address64 linear_address, Qword paging_generation) noexcept;
    [[nodiscard]] const PageTranslation* lookup(Address64 linear_address, Qword paging_generation) const noexcept;
    void insert(PageTranslation translation, Qword paging_generation) noexcept;

private:
    struct Entry
    {
        PageTranslation translation;
        Qword paging_generation = Qword{0};
        bool valid = false;
    };

    [[nodiscard]] static std::size_t slot_index(Address64 linear_address) noexcept;

    std::array<Entry, TRANSLATION_LOOKASIDE_BUFFER_ENTRY_COUNT> entries_{};
    std::size_t size_ = 0;
};
}
