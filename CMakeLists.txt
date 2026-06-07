cmake_minimum_required(VERSION 3.20)

project(MNOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MNOS_ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation." OFF)

enable_testing()

function(mnos_apply_project_options target_name)
    target_compile_options(
        ${target_name}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wextra>
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wpedantic>
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wconversion>
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wsign-conversion>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )

    if(MNOS_ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target_name} PRIVATE -O0 -g --coverage)
            target_link_options(${target_name} PRIVATE --coverage)
        else()
            message(WARNING "MNOS_ENABLE_COVERAGE is only configured for GCC/Clang-like compilers.")
        endif()
    endif()
endfunction()

add_library(
    mnos_cpu
    STATIC
        src/cpu/common/data_size.cpp
        src/cpu/register/id.cpp
        src/cpu/register/bank.cpp
        src/cpu/flags/id.cpp
        src/cpu/flags/rflags.cpp
        src/cpu/instruction/opcode.cpp
        src/cpu/instruction/operand.cpp
        src/cpu/instruction/instruction.cpp
)

target_compile_features(mnos_cpu PUBLIC cxx_std_20)

target_include_directories(
    mnos_cpu
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

mnos_apply_project_options(mnos_cpu)

add_executable(mnos_emulator src/emulator/main.cpp)
target_link_libraries(mnos_emulator PRIVATE mnos_cpu)
mnos_apply_project_options(mnos_emulator)

add_executable(mnos_cpu_tests tests/cpu/core_test.cpp)
target_link_libraries(mnos_cpu_tests PRIVATE mnos_cpu)
mnos_apply_project_options(mnos_cpu_tests)

add_test(NAME mnos_cpu_tests COMMAND mnos_cpu_tests)
