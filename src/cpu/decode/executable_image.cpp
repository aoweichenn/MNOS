#include <stdexcept>
#include <utility>

#include <mnos/cpu/decode/executable_image.hpp>

namespace
{
constexpr const char* EXECUTABLE_IMAGE_RIP_OUT_OF_RANGE_MESSAGE = "executable image rip is out of range";
}

namespace mnos::cpu
{
ExecutableImage::ExecutableImage(container_type bytes) noexcept : bytes_(std::move(bytes))
{
}

ExecutableImage::ExecutableImage(container_type bytes, const InstructionPointer base_rip) noexcept :
    bytes_(std::move(bytes)), base_rip_(base_rip)
{
}

ExecutableImage::ExecutableImage(std::initializer_list<Byte> bytes) : bytes_(bytes)
{
}

bool ExecutableImage::empty() const noexcept
{
    return this->bytes_.empty();
}

std::size_t ExecutableImage::size() const noexcept
{
    return this->bytes_.size();
}

InstructionPointer ExecutableImage::base_rip() const noexcept
{
    return this->base_rip_;
}

InstructionPointer ExecutableImage::end_rip() const noexcept
{
    return this->base_rip_ + static_cast<InstructionPointer>(this->bytes_.size());
}

bool ExecutableImage::contains_rip(const InstructionPointer rip) const noexcept
{
    return rip >= this->base_rip_ &&
        (rip - this->base_rip_) < static_cast<InstructionPointer>(this->bytes_.size());
}

std::size_t ExecutableImage::offset_of(const InstructionPointer rip) const
{
    if (!this->contains_rip(rip))
    {
        throw std::out_of_range{EXECUTABLE_IMAGE_RIP_OUT_OF_RANGE_MESSAGE};
    }

    return static_cast<std::size_t>(rip - this->base_rip_);
}

Byte ExecutableImage::byte_at(const InstructionPointer rip) const
{
    return this->bytes_[this->offset_of(rip)];
}

std::span<const Byte> ExecutableImage::bytes() const noexcept
{
    return std::span<const Byte>{this->bytes_};
}
}
