cmake_minimum_required(VERSION 3.21)

project(Cieto VERSION 0.2.0 LANGUAGES C)

include(GNUInstallDirs)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(FATAL_ERROR
        "Cieto requires GCC because it uses GNU C extensions. "
        "Configure CMake with a GCC-based toolchain or preset."
    )
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS "core/*.c")
file(GLOB_RECURSE VM_SRC CONFIGURE_DEPENDS "vm/*.c")
file(GLOB_RECURSE COMPILER_SRC CONFIGURE_DEPENDS "compiler/*.c")
file(GLOB_RECURSE STD_SRC CONFIGURE_DEPENDS "std/*.c")
file(GLOB_RECURSE CMD_SRC CONFIGURE_DEPENDS "cmd/*.c")

set(CIETO_INTERNAL_INCLUDE_DIRS
    cmd
    core
    vm
    compiler
    std
    vendor/xxhash
)

set(CIETO_API_SRC
    api/cieto.c
)

set(CIETO_VENDOR_SRC
    vendor/xxhash/xxhash.c
)

set(CIETO_RUNTIME_SRC
    ${CIETO_API_SRC}
    ${CIETO_VENDOR_SRC}
    ${CORE_SRC}
    ${VM_SRC}
    ${COMPILER_SRC}
    ${STD_SRC}
)

add_compile_options(
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O3>
)

add_library(libcieto STATIC
    ${CIETO_RUNTIME_SRC}
)

target_include_directories(libcieto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CIETO_INTERNAL_INCLUDE_DIRS}
)

set_target_properties(libcieto PROPERTIES
    OUTPUT_NAME cieto
)

target_link_libraries(libcieto
    PRIVATE
        m
)

add_executable(cieto
    ${CMD_SRC}
)

target_include_directories(cieto
    PRIVATE
        ${CIETO_INTERNAL_INCLUDE_DIRS}
)

target_link_libraries(cieto
    PRIVATE
        libcieto
)

if(NOT WIN32)
    add_library(linenoise_lib STATIC
        vendor/linenoise/linenoise.c
    )

    target_include_directories(linenoise_lib
        PUBLIC
            vendor/linenoise
    )

    target_compile_definitions(cieto
        PRIVATE
            USE_LINENOISE
    )

    target_link_libraries(cieto
        PRIVATE
            linenoise_lib
    )
endif()

add_executable(cieto_embed_basic
    examples/embedding/basic.c
)

target_link_libraries(cieto_embed_basic
    PRIVATE
        libcieto
)

add_executable(cieto_embed_exit_policy
    tests/embedding_exit_policy.c
)

target_link_libraries(cieto_embed_exit_policy
    PRIVATE
        libcieto
)

add_executable(cieto_embed_native_function
    tests/embedding_native_function.c
)

target_link_libraries(cieto_embed_native_function
    PRIVATE
        libcieto
)

add_executable(cieto_embed_output_callback
    tests/embedding_output_callback.c
)

target_link_libraries(cieto_embed_output_callback
    PRIVATE
        libcieto
)

add_executable(cieto_embed_call_script
    tests/embedding_call_script.c
)

target_link_libraries(cieto_embed_call_script
    PRIVATE
        libcieto
)

include(CTest)

if(BUILD_TESTING)
    if(WIN32)
        find_program(
            CIETO_POWERSHELL_EXECUTABLE
            NAMES powershell.exe powershell pwsh.exe pwsh
        )

        if(NOT CIETO_POWERSHELL_EXECUTABLE)
            message(FATAL_ERROR "PowerShell was not found.")
        endif()

        add_test(
            NAME cieto_script_tests
            COMMAND
                "${CIETO_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run_tests.ps1"
        )

        set_tests_properties(cieto_script_tests PROPERTIES
            ENVIRONMENT "CIETO_EXEC=$<TARGET_FILE:cieto>;TIMEOUT_SEC=60"
        )

        add_test(
            NAME cieto_script_tests_no_opt
            COMMAND
                "${CIETO_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run_tests.ps1"
        )

        set_tests_properties(cieto_script_tests_no_opt PROPERTIES
            ENVIRONMENT "CIETO_EXEC=$<TARGET_FILE:cieto>;TIMEOUT_SEC=60;CIETO_COMPARE_ARGS=--no-opt"
        )
    else()
        find_program(
            CIETO_BASH_EXECUTABLE
            NAMES bash
        )

        if(NOT CIETO_BASH_EXECUTABLE)
            message(FATAL_ERROR "Bash was not found.")
        endif()

        add_test(
            NAME cieto_script_tests
            COMMAND
                "${CIETO_BASH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/run_tests.sh"
                "$<TARGET_FILE:cieto>"
                "60"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tests"
        )

        add_test(
            NAME cieto_script_tests_no_opt
            COMMAND
                "${CIETO_BASH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/run_tests.sh"
                "$<TARGET_FILE:cieto>"
                "60"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tests"
        )

        set_tests_properties(cieto_script_tests_no_opt PROPERTIES
            ENVIRONMENT "CIETO_COMPARE_ARGS=--no-opt"
        )
    endif()

    add_test(
        NAME cieto_dump_no_opt
        COMMAND $<TARGET_FILE:cieto> --no-opt --dump "${CMAKE_SOURCE_DIR}/tests/test_constant_folding.cies"
    )

    add_test(
        NAME cieto_embedding_basic
        COMMAND $<TARGET_FILE:cieto_embed_basic>
    )

    add_test(
        NAME cieto_embedding_exit_policy
        COMMAND $<TARGET_FILE:cieto_embed_exit_policy>
    )

    add_test(
        NAME cieto_embedding_native_function
        COMMAND $<TARGET_FILE:cieto_embed_native_function>
    )

    add_test(
        NAME cieto_embedding_call_script
        COMMAND $<TARGET_FILE:cieto_embed_call_script>
    )

    add_test(
        NAME cieto_embedding_output_callback
        COMMAND $<TARGET_FILE:cieto_embed_output_callback>
    )
endif()

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS
        cieto
        cieto_embed_basic
        cieto_embed_exit_policy
        cieto_embed_native_function
        cieto_embed_call_script
        cieto_embed_output_callback
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

install(TARGETS cieto libcieto
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES include/cieto.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES README.md manual.md LICENSE gpl-3.0.txt
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples
    FILES_MATCHING
        PATTERN "*.cies"
        PATTERN "*.c"
        PATTERN "README.md"
)

message(STATUS "Project 'Cieto' configured to use C Compiler: ${CMAKE_C_COMPILER}")
