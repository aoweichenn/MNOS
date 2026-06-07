#include <mnos/cpu/memory/tlb.hpp>

namespace mnos::cpu::memory
{
bool TranslationLookasideBuffer::empty() const noexcept
{
    return this->size_ == std::size_t{0};
}

std::size_t TranslationLookasideBuffer::size() const noexcept
{
    return this->size_;
}

std::size_t TranslationLookasideBuffer::capacity() const noexcept
{
    return this->entries_.size();
}

void TranslationLookasideBuffer::clear() noexcept
{
    for (Entry& entry : this->entries_)
    {
        entry.valid = false;
    }
    this->size_ = std::size_t{0};
}

void TranslationLookasideBuffer::invalidate_page(const Address64 linear_address) noexcept
{
    for (Entry& entry : this->entries_)
    {
        if (entry.valid && entry.translation.contains(linear_address))
        {
            entry.valid = false;
            --this->size_;
        }
    }
}

PageTranslation* TranslationLookasideBuffer::lookup(
    const Address64 linear_address,
    const Qword paging_generation) noexcept
{
    Entry& entry = this->entries_[slot_index(linear_address)];
    if (!entry.valid || entry.paging_generation != paging_generation || !entry.translation.contains(linear_address))
    {
        return nullptr;
    }
    return &entry.translation;
}

const PageTranslation* TranslationLookasideBuffer::lookup(
    const Address64 linear_address,
    const Qword paging_generation) const noexcept
{
    const Entry& entry = this->entries_[slot_index(linear_address)];
    if (!entry.valid || entry.paging_generation != paging_generation || !entry.translation.contains(linear_address))
    {
        return nullptr;
    }
    return &entry.translation;
}

void TranslationLookasideBuffer::insert(PageTranslation translation, const Qword paging_generation) noexcept
{
    Entry& entry = this->entries_[slot_index(translation.virtual_page_base())];
    if (!entry.valid)
    {
        ++this->size_;
    }
    entry.translation = translation;
    entry.paging_generation = paging_generation;
    entry.valid = true;
}

std::size_t TranslationLookasideBuffer::slot_index(const Address64 linear_address) noexcept
{
    return static_cast<std::size_t>((linear_address >> PAGE_TABLE_OFFSET_BITS_4K) %
                                    TRANSLATION_LOOKASIDE_BUFFER_ENTRY_COUNT);
}
}
