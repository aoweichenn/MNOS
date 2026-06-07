#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace mnos::test
{
inline void check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error{std::string{message}};
    }
}

template <typename Exception, typename Callable>
void check_throws(Callable&& callable, const std::string_view message)
{
    try
    {
        std::forward<Callable>(callable)();
    }
    catch (const Exception&)
    {
        return;
    }
    catch (...)
    {
        throw std::runtime_error{"unexpected exception type: " + std::string{message}};
    }

    throw std::runtime_error{"expected exception was not thrown: " + std::string{message}};
}
}
