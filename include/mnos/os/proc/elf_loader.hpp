#pragma once

#include <span>

#include <mnos/cpu/decode/executable_image.hpp>
#include <mnos/os/proc/user_loader.hpp>

namespace mnos::os::proc
{
class LoadedUserExecutable final
{
public:
    LoadedUserExecutable(UserProgram program, cpu::ExecutableImage executable_image);

    [[nodiscard]] const UserProgram& program() const noexcept;
    [[nodiscard]] const cpu::ExecutableImage& executable_image() const noexcept;

private:
    UserProgram program_;
    cpu::ExecutableImage executable_image_;
};

class Elf64Loader final
{
public:
    [[nodiscard]] LoadedUserExecutable load(std::span<const cpu::Byte> file_bytes) const;
};
}
