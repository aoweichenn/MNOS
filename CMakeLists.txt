cmake_minimum_required(VERSION 3.20)
project(MNOS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(smoke_test
        tests/smoke_test.cpp
        include/mnos/cpu/common/types.hpp
        include/mnos/cpu/common/data_size.hpp
        source/cpu/common/data_size.cpp
        include/mnos/cpu/register/id.hpp
        source/cpu/register/id.cpp
        include/mnos/cpu/register/bank.hpp
        source/cpu/register/bank.cpp
        include/mnos/cpu/flags/id.hpp
        source/cpu/flags/id.cpp
        source/cpu/flags/rflags.cpp
        include/mnos/cpu/flags/rflags.hpp
        include/mnos/cpu/instruction/opcode.hpp
        source/cpu/instruction/opcode.cpp
        include/mnos/cpu/instruction/operand.hpp
        source/cpu/instruction/operand.cpp
        include/mnos/cpu/instruction/instrcution.hpp
        source/cpu/instruction/instruction.cpp
)

target_compile_options(
        smoke_test
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
)

add_test(
        NAME smoke_test
        COMMAND smoke_test
)
