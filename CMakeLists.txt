cmake_minimum_required(VERSION 3.20)

project(MNOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MNOS_ENABLE_COVERAGE "Enable GCC/Clang coverage instrumentation." OFF)
option(MNOS_USE_SYSTEM_GTEST "Use an installed GTest package instead of the vendored FetchContent copy." OFF)
option(MNOS_USE_SYSTEM_BENCHMARK "Use an installed Google Benchmark package instead of the vendored FetchContent copy." OFF)

enable_testing()

include(FetchContent)

if(MNOS_USE_SYSTEM_GTEST)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        find_package(GTest QUIET)
    endif()
endif()
if(NOT MNOS_USE_SYSTEM_GTEST OR NOT GTest_FOUND)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

if(MNOS_USE_SYSTEM_BENCHMARK)
    find_package(benchmark CONFIG QUIET)
endif()
if(NOT MNOS_USE_SYSTEM_BENCHMARK OR NOT benchmark_FOUND)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googlebenchmark)
endif()

foreach(MNOS_BENCHMARK_DEPENDENCY_TARGET benchmark benchmark_main)
    if(TARGET ${MNOS_BENCHMARK_DEPENDENCY_TARGET})
        target_compile_options(
            ${MNOS_BENCHMARK_DEPENDENCY_TARGET}
            PRIVATE
                $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-c2y-extensions>
        )
    endif()
endforeach()

include(GoogleTest)

add_library(mnos_gtest_gmock_main INTERFACE)
if(TARGET GTest::gmock_main)
    target_link_libraries(mnos_gtest_gmock_main INTERFACE GTest::gmock_main)
elseif(TARGET GTest::gmock AND TARGET GTest::gtest_main)
    target_link_libraries(mnos_gtest_gmock_main INTERFACE GTest::gmock GTest::gtest_main)
elseif(TARGET GTest::gtest_main)
    target_link_libraries(mnos_gtest_gmock_main INTERFACE GTest::gtest_main)
else()
    message(FATAL_ERROR "GTest/GMock targets were not found.")
endif()

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

set(
    MNOS_CPU_SOURCES
        src/cpu/common/data_size.cpp
        src/cpu/decode/decoder.cpp
        src/cpu/decode/executable_image.cpp
        src/cpu/register/id.cpp
        src/cpu/register/bank.cpp
        src/cpu/flags/id.cpp
        src/cpu/flags/rflags.cpp
        src/cpu/instruction/condition_code.cpp
        src/cpu/instruction/opcode.cpp
        src/cpu/instruction/operand.cpp
        src/cpu/instruction/instruction.cpp
        src/cpu/memory/physical_memory.cpp
        src/cpu/memory/memory_bus.cpp
        src/cpu/memory/cache.cpp
        src/cpu/memory/memory_model.cpp
        src/cpu/memory/paging.cpp
        src/cpu/memory/tlb.cpp
        src/cpu/memory/tlb_shootdown.cpp
        src/cpu/memory/page_table_walker.cpp
        src/cpu/memory/page_table_builder.cpp
        src/cpu/memory/mmu.cpp
        src/cpu/system/core_topology.cpp
        src/cpu/system/apic.cpp
        src/cpu/system/privilege.cpp
        src/cpu/system/interrupt_vector.cpp
        src/cpu/system/descriptor_tables.cpp
        src/cpu/system/trap_frame.cpp
        src/cpu/system/trap_controller.cpp
        src/cpu/execution/pipeline.cpp
        src/cpu/execution/cpu_state.cpp
        src/cpu/execution/program.cpp
        src/cpu/execution/trace.cpp
        src/cpu/execution/executor.cpp
        src/cpu/perf/performance_model.cpp
)

add_library(mnos_cpu STATIC ${MNOS_CPU_SOURCES})

target_compile_features(mnos_cpu PUBLIC cxx_std_20)

target_include_directories(
    mnos_cpu
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

mnos_apply_project_options(mnos_cpu)

set(
    MNOS_OS_SOURCES
        src/os/kernel/boot_context.cpp
        src/os/kernel/kernel.cpp
        src/os/kernel/syscall.cpp
        src/os/kernel/user_trap.cpp
        src/os/dev/terminal.cpp
        src/os/io/file_descriptor.cpp
        src/os/mm/address_layout.cpp
        src/os/mm/address_space.cpp
        src/os/mm/page.cpp
        src/os/mm/page_fault_handler.cpp
        src/os/mm/physical_page_allocator.cpp
        src/os/platform/machine.cpp
        src/os/proc/copy_on_write.cpp
        src/os/proc/futex.cpp
        src/os/proc/process.cpp
        src/os/proc/user_loader.cpp
        src/os/sched/event.cpp
        src/os/sched/round_robin_scheduler.cpp
        src/os/sched/smp_scheduler.cpp
        src/os/sched/sleep_queue.cpp
        src/os/sched/wait_queue.cpp
        src/os/sched/thread_state.cpp
        src/os/sched/thread_context.cpp
        src/os/shell/shell.cpp
        src/os/tty/console.cpp
)

add_library(mnos_os STATIC ${MNOS_OS_SOURCES})

target_compile_features(mnos_os PUBLIC cxx_std_20)

target_include_directories(
    mnos_os
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(mnos_os PUBLIC mnos_cpu)

mnos_apply_project_options(mnos_os)

add_executable(mnos_emulator src/emulator/main.cpp)
target_link_libraries(mnos_emulator PRIVATE mnos_os)
mnos_apply_project_options(mnos_emulator)

set(
    MNOS_BENCHMARK_SOURCES
        benchmarks/cpu_executor_benchmark.cpp
        benchmarks/os_stage0_benchmark.cpp
)

add_executable(mnos_benchmarks ${MNOS_BENCHMARK_SOURCES})
target_link_libraries(mnos_benchmarks PRIVATE mnos_os benchmark::benchmark_main)
target_include_directories(mnos_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/tests)
mnos_apply_project_options(mnos_benchmarks)
target_compile_options(
    mnos_benchmarks
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-c2y-extensions>
)

add_custom_target(
    mnos_benchmark_smoke
    COMMAND $<TARGET_FILE:mnos_benchmarks> --benchmark_min_time=0.001s --benchmark_repetitions=1
    DEPENDS mnos_benchmarks
    COMMENT "Running a short Google Benchmark smoke test"
)

set(MNOS_CPU_TEST_TARGETS)
set(MNOS_CPU_COVERAGE_TEST_GCDA_FILES)
set(MNOS_OS_TEST_TARGETS)
set(MNOS_OS_COVERAGE_TEST_GCDA_FILES)

function(mnos_add_module_test target_name source_file linked_target test_targets_variable test_gcda_files_variable)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${linked_target} mnos_gtest_gmock_main)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    mnos_apply_project_options(${target_name})
    gtest_discover_tests(${target_name} DISCOVERY_MODE PRE_TEST)

    set(MNOS_UPDATED_TEST_TARGETS ${${test_targets_variable}})
    list(APPEND MNOS_UPDATED_TEST_TARGETS ${target_name})
    set(${test_targets_variable} "${MNOS_UPDATED_TEST_TARGETS}" PARENT_SCOPE)

    set(MNOS_UPDATED_TEST_GCDA_FILES ${${test_gcda_files_variable}})
    list(
        APPEND
        MNOS_UPDATED_TEST_GCDA_FILES
            ${CMAKE_BINARY_DIR}/CMakeFiles/${target_name}.dir/${source_file}.gcda
    )
    set(${test_gcda_files_variable} "${MNOS_UPDATED_TEST_GCDA_FILES}" PARENT_SCOPE)
endfunction()

macro(mnos_add_cpu_test target_name source_file)
    mnos_add_module_test(
        ${target_name}
        ${source_file}
        mnos_cpu
        MNOS_CPU_TEST_TARGETS
        MNOS_CPU_COVERAGE_TEST_GCDA_FILES
    )
endmacro()

macro(mnos_add_os_test target_name source_file)
    mnos_add_module_test(
        ${target_name}
        ${source_file}
        mnos_os
        MNOS_OS_TEST_TARGETS
        MNOS_OS_COVERAGE_TEST_GCDA_FILES
    )
endmacro()

mnos_add_cpu_test(mnos_cpu_common_unit_tests tests/cpu/unit/common_data_size_test.cpp)
mnos_add_cpu_test(mnos_cpu_register_flags_unit_tests tests/cpu/unit/register_flags_test.cpp)
mnos_add_cpu_test(mnos_cpu_instruction_unit_tests tests/cpu/unit/instruction_test.cpp)
mnos_add_cpu_test(mnos_cpu_system_unit_tests tests/cpu/unit/system_test.cpp)
mnos_add_cpu_test(mnos_cpu_decoder_unit_tests tests/cpu/unit/decoder_test.cpp)
mnos_add_cpu_test(mnos_cpu_execution_state_unit_tests tests/cpu/unit/execution_state_test.cpp)
mnos_add_cpu_test(mnos_cpu_memory_unit_tests tests/cpu/unit/memory_test.cpp)
mnos_add_cpu_test(mnos_cpu_cache_unit_tests tests/cpu/unit/cache_test.cpp)
mnos_add_cpu_test(mnos_cpu_memory_model_unit_tests tests/cpu/unit/memory_model_test.cpp)
mnos_add_cpu_test(mnos_cpu_paging_unit_tests tests/cpu/unit/paging_test.cpp)
mnos_add_cpu_test(mnos_cpu_pipeline_unit_tests tests/cpu/unit/pipeline_test.cpp)
mnos_add_cpu_test(mnos_cpu_performance_unit_tests tests/cpu/unit/performance_model_test.cpp)
mnos_add_cpu_test(mnos_cpu_apic_unit_tests tests/cpu/unit/apic_test.cpp)
mnos_add_cpu_test(mnos_cpu_tlb_shootdown_unit_tests tests/cpu/unit/tlb_shootdown_test.cpp)
mnos_add_cpu_test(mnos_cpu_executor_integration_tests tests/cpu/integration/executor_program_test.cpp)
mnos_add_cpu_test(mnos_cpu_executor_error_integration_tests tests/cpu/integration/executor_error_test.cpp)
mnos_add_cpu_test(mnos_cpu_byte_executor_integration_tests tests/cpu/integration/byte_executor_test.cpp)
mnos_add_cpu_test(mnos_cpu_stage8_integration_tests tests/cpu/integration/stage8_performance_test.cpp)
mnos_add_cpu_test(mnos_cpu_executor_chaos_tests tests/cpu/chaos/executor_chaos_test.cpp)
mnos_add_cpu_test(mnos_cpu_stage8_chaos_tests tests/cpu/chaos/stage8_cache_chaos_test.cpp)
mnos_add_cpu_test(mnos_cpu_memory_executor_fuzz_tests tests/cpu/fuzz/memory_executor_fuzz_test.cpp)

mnos_add_os_test(mnos_os_address_page_unit_tests tests/os/unit/address_page_test.cpp)
mnos_add_os_test(mnos_os_physical_page_allocator_unit_tests tests/os/unit/physical_page_allocator_test.cpp)
mnos_add_os_test(mnos_os_address_space_unit_tests tests/os/unit/address_space_test.cpp)
mnos_add_os_test(mnos_os_page_fault_handler_unit_tests tests/os/unit/page_fault_handler_test.cpp)
mnos_add_os_test(mnos_os_kernel_boot_context_unit_tests tests/os/unit/kernel_boot_context_test.cpp)
mnos_add_os_test(mnos_os_thread_context_unit_tests tests/os/unit/thread_context_test.cpp)
mnos_add_os_test(mnos_os_process_scheduler_unit_tests tests/os/unit/process_scheduler_test.cpp)
mnos_add_os_test(mnos_os_stage7_sched_unit_tests tests/os/unit/stage7_sched_test.cpp)
mnos_add_os_test(mnos_os_smp_scheduler_unit_tests tests/os/unit/smp_scheduler_test.cpp)
mnos_add_os_test(mnos_os_stage10_user_loader_unit_tests tests/os/unit/stage10_user_loader_test.cpp)
mnos_add_os_test(mnos_os_stage10_sync_cow_unit_tests tests/os/unit/stage10_sync_cow_test.cpp)
mnos_add_os_test(mnos_os_stage11_syscall_abi_unit_tests tests/os/unit/stage11_syscall_abi_test.cpp)
mnos_add_os_test(mnos_os_stage12_terminal_unit_tests tests/os/unit/stage12_terminal_test.cpp)
mnos_add_os_test(mnos_os_stage13_io_shell_unit_tests tests/os/unit/stage13_io_shell_test.cpp)
mnos_add_os_test(mnos_os_kernel_boot_integration_tests tests/os/integration/kernel_boot_integration_test.cpp)
mnos_add_os_test(mnos_os_stage5_integration_tests tests/os/integration/stage5_integration_test.cpp)
mnos_add_os_test(mnos_os_stage7_integration_tests tests/os/integration/stage7_integration_test.cpp)
mnos_add_os_test(mnos_os_stage9_integration_tests tests/os/integration/stage9_smp_scheduler_test.cpp)
mnos_add_os_test(mnos_os_stage10_integration_tests tests/os/integration/stage10_user_process_test.cpp)
mnos_add_os_test(mnos_os_stage11_integration_tests tests/os/integration/stage11_syscall_trap_test.cpp)
mnos_add_os_test(mnos_os_stage12_integration_tests tests/os/integration/stage12_terminal_console_test.cpp)
mnos_add_os_test(mnos_os_stage13_integration_tests tests/os/integration/stage13_shell_io_test.cpp)
mnos_add_os_test(mnos_os_thread_context_chaos_tests tests/os/chaos/thread_context_chaos_test.cpp)
mnos_add_os_test(mnos_os_scheduler_chaos_tests tests/os/chaos/scheduler_chaos_test.cpp)
mnos_add_os_test(mnos_os_stage7_chaos_tests tests/os/chaos/stage7_scheduler_chaos_test.cpp)
mnos_add_os_test(mnos_os_stage9_chaos_tests tests/os/chaos/stage9_smp_scheduler_chaos_test.cpp)
mnos_add_os_test(mnos_os_stage10_chaos_tests tests/os/chaos/stage10_sync_chaos_test.cpp)
mnos_add_os_test(mnos_os_address_page_fuzz_tests tests/os/fuzz/address_page_fuzz_test.cpp)
mnos_add_os_test(mnos_os_physical_page_allocator_fuzz_tests tests/os/fuzz/physical_page_allocator_fuzz_test.cpp)

if(MNOS_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(MNOS_GCOV_EXECUTABLE gcov)

    if(MNOS_GCOV_EXECUTABLE)
        function(mnos_collect_target_coverage_files target_name source_files_variable gcno_files_variable gcda_files_variable)
            set(MNOS_LOCAL_GCNO_FILES)
            set(MNOS_LOCAL_GCDA_FILES)
            foreach(MNOS_SOURCE_FILE IN LISTS ${source_files_variable})
                list(
                    APPEND
                    MNOS_LOCAL_GCNO_FILES
                        ${CMAKE_BINARY_DIR}/CMakeFiles/${target_name}.dir/${MNOS_SOURCE_FILE}.gcno
                )
                list(
                    APPEND
                    MNOS_LOCAL_GCDA_FILES
                        ${CMAKE_BINARY_DIR}/CMakeFiles/${target_name}.dir/${MNOS_SOURCE_FILE}.gcda
                )
            endforeach()
            set(${gcno_files_variable} "${MNOS_LOCAL_GCNO_FILES}" PARENT_SCOPE)
            set(${gcda_files_variable} "${MNOS_LOCAL_GCDA_FILES}" PARENT_SCOPE)
        endfunction()

        function(mnos_create_coverage_target
                 target_name
                 report_subdirectory
                 test_targets_variable
                 gcno_files_variable
                 gcda_files_variable)
            set(MNOS_COVERAGE_TEST_COMMANDS)
            foreach(MNOS_TEST_TARGET IN LISTS ${test_targets_variable})
                list(APPEND MNOS_COVERAGE_TEST_COMMANDS COMMAND $<TARGET_FILE:${MNOS_TEST_TARGET}>)
            endforeach()

            add_custom_target(
                ${target_name}
                COMMAND ${CMAKE_COMMAND} -E rm -f ${${gcda_files_variable}}
                ${MNOS_COVERAGE_TEST_COMMANDS}
                COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/coverage/gcov/${report_subdirectory}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage/gcov/${report_subdirectory}
                COMMAND ${CMAKE_COMMAND} -E chdir
                        ${CMAKE_BINARY_DIR}/coverage/gcov/${report_subdirectory}
                        ${MNOS_GCOV_EXECUTABLE} -b -c -r -s ${PROJECT_SOURCE_DIR} ${${gcno_files_variable}}
                DEPENDS ${${test_targets_variable}}
                COMMENT "Writing ${target_name} gcov reports to ${CMAKE_BINARY_DIR}/coverage/gcov/${report_subdirectory}"
            )
            set(MNOS_COVERAGE_DEPENDENCY_TARGETS ${${test_targets_variable}})
            if(MNOS_COVERAGE_DEPENDENCY_TARGETS)
                add_dependencies(${target_name} ${MNOS_COVERAGE_DEPENDENCY_TARGETS})
            endif()
        endfunction()

        mnos_collect_target_coverage_files(
            mnos_cpu
            MNOS_CPU_SOURCES
            MNOS_CPU_COVERAGE_GCNO_FILES
            MNOS_CPU_COVERAGE_LIBRARY_GCDA_FILES
        )
        mnos_collect_target_coverage_files(
            mnos_os
            MNOS_OS_SOURCES
            MNOS_OS_COVERAGE_GCNO_FILES
            MNOS_OS_COVERAGE_LIBRARY_GCDA_FILES
        )

        set(
            MNOS_CPU_COVERAGE_GCDA_FILES
                ${MNOS_CPU_COVERAGE_LIBRARY_GCDA_FILES}
                ${MNOS_CPU_COVERAGE_TEST_GCDA_FILES}
        )
        set(
            MNOS_OS_COVERAGE_GCDA_FILES
                ${MNOS_OS_COVERAGE_LIBRARY_GCDA_FILES}
                ${MNOS_OS_COVERAGE_TEST_GCDA_FILES}
        )
        set(MNOS_ALL_TEST_TARGETS ${MNOS_CPU_TEST_TARGETS} ${MNOS_OS_TEST_TARGETS})
        set(MNOS_ALL_COVERAGE_GCNO_FILES ${MNOS_CPU_COVERAGE_GCNO_FILES} ${MNOS_OS_COVERAGE_GCNO_FILES})
        set(MNOS_ALL_COVERAGE_GCDA_FILES ${MNOS_CPU_COVERAGE_GCDA_FILES} ${MNOS_OS_COVERAGE_GCDA_FILES})

        mnos_create_coverage_target(
            mnos_cpu_coverage
            cpu
            MNOS_CPU_TEST_TARGETS
            MNOS_CPU_COVERAGE_GCNO_FILES
            MNOS_CPU_COVERAGE_GCDA_FILES
        )
        mnos_create_coverage_target(
            mnos_os_coverage
            os
            MNOS_OS_TEST_TARGETS
            MNOS_OS_COVERAGE_GCNO_FILES
            MNOS_OS_COVERAGE_GCDA_FILES
        )
        mnos_create_coverage_target(
            mnos_coverage
            all
            MNOS_ALL_TEST_TARGETS
            MNOS_ALL_COVERAGE_GCNO_FILES
            MNOS_ALL_COVERAGE_GCDA_FILES
        )
    else()
        message(WARNING "gcov was not found; coverage targets will not be available.")
    endif()
endif()
