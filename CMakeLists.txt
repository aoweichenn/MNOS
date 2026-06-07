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
        src/cpu/memory/physical_memory.cpp
        src/cpu/memory/memory_bus.cpp
        src/cpu/execution/cpu_state.cpp
        src/cpu/execution/program.cpp
        src/cpu/execution/trace.cpp
        src/cpu/execution/executor.cpp
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

if(MNOS_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(MNOS_GCOV_EXECUTABLE gcov)

    if(MNOS_GCOV_EXECUTABLE)
        set(
            MNOS_CPU_COVERAGE_GCNO_FILES
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/common/data_size.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/register/id.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/register/bank.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/flags/id.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/flags/rflags.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/opcode.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/operand.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/instruction.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/memory/physical_memory.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/memory/memory_bus.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/cpu_state.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/program.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/trace.cpp.gcno
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/executor.cpp.gcno
        )

        set(
            MNOS_CPU_COVERAGE_GCDA_FILES
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/common/data_size.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/register/id.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/register/bank.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/flags/id.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/flags/rflags.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/opcode.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/operand.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/instruction/instruction.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/memory/physical_memory.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/memory/memory_bus.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/cpu_state.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/program.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/trace.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu.dir/src/cpu/execution/executor.cpp.gcda
                ${CMAKE_BINARY_DIR}/CMakeFiles/mnos_cpu_tests.dir/tests/cpu/core_test.cpp.gcda
        )

        add_custom_target(
            mnos_cpu_coverage
            COMMAND ${CMAKE_COMMAND} -E rm -f ${MNOS_CPU_COVERAGE_GCDA_FILES}
            COMMAND $<TARGET_FILE:mnos_cpu_tests>
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/coverage/gcov
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage/gcov
            COMMAND ${CMAKE_COMMAND} -E chdir
                    ${CMAKE_BINARY_DIR}/coverage/gcov
                    ${MNOS_GCOV_EXECUTABLE} -b -c -r -s ${PROJECT_SOURCE_DIR} ${MNOS_CPU_COVERAGE_GCNO_FILES}
            DEPENDS mnos_cpu_tests
            COMMENT "Writing mnos_cpu gcov reports to ${CMAKE_BINARY_DIR}/coverage/gcov"
        )
    else()
        message(WARNING "gcov was not found; mnos_cpu_coverage target will not be available.")
    endif()
endif()
