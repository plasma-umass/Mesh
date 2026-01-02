#Include current folder to guarantee headers are found by files
include_directories(./)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../heap_layers-src)
# include_directories(./vendor/Heap-Layers)

#Create a common set of source files (cross-platform core)
set(common_src
        d_assert.cc
        global_heap.cc
        meshable_arena.cc
        measure_rss.cc
        thread_local_heap.cc
        printf.c
        putchar.c
        )

# Add platform-specific source files
if(WIN32)
    # Windows: use Windows-specific implementations
    list(APPEND common_src
        runtime_windows.cc
        memory_stats_windows.cc
        platform/vmem_windows.cc
        platform/exception_handler_windows.cc
    )
elseif(APPLE)
    list(APPEND common_src
        runtime.cc
        real.cc
        memory_stats_macos.cc
    )
else()
    # Linux/Unix
    list(APPEND common_src
        runtime.cc
        real.cc
        memory_stats_linux.cc
    )
endif()

#Create the set of source files for the mesh library
set(mesh_src
        ${common_src}
        libmesh.cc
        )

#Add a target for the mesh shared library
add_library(mesh SHARED ${mesh_src})

# On Windows, disable auto-export - we use explicit __declspec(dllexport) via MESH_EXPORT
if(WIN32)
    set_target_properties(mesh PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()

# Platform-specific link libraries
if(WIN32)
    target_link_libraries(mesh PRIVATE kernel32 psapi advapi32)
elseif(APPLE)
    target_link_libraries(mesh PRIVATE pthread dl)
else()
    target_link_libraries(mesh PRIVATE pthread dl rt m)
endif()

# Add static library target
add_library(mesh_static STATIC ${mesh_src})
set_target_properties(mesh_static PROPERTIES OUTPUT_NAME "mesh")
if(WIN32)
    target_link_libraries(mesh_static PRIVATE kernel32 psapi advapi32)
elseif(APPLE)
    target_link_libraries(mesh_static PRIVATE pthread dl)
else()
    target_link_libraries(mesh_static PRIVATE pthread dl rt m)
endif()

# Windows fragmenter test
if(WIN32)
    add_executable(fragmenter_test testing/fragmenter_windows.cc measure_rss.cc)
    target_link_libraries(fragmenter_test PRIVATE mesh_static kernel32 psapi advapi32)
    target_include_directories(fragmenter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Skip unit tests on Windows for now (googletest ExternalProject setup needs work)
if(NOT WIN32)

#Create a set of source files for the unit tests
set(unit_src
        ${common_src}
        ${google_src}
        testing/unit/alignment.cc
        testing/unit/bitmap_test.cc
        testing/unit/concurrent_mesh_test.cc
        testing/unit/mesh_memory_test.cc
        testing/unit/mesh_test.cc
        testing/unit/pending_list_test.cc
        testing/unit/rng_test.cc
        testing/unit/thread_exit_test.cc
        testing/unit/size_class_test.cc
        testing/unit/triple_mesh_test.cc
)

# Add macOS-specific tests only on Apple platforms
if(APPLE)
    list(APPEND unit_src testing/unit/macos_memory_test.cc)
endif()

#Add a target to the unit tests executable
add_executable(unit.test ${unit_src})

# Platform-specific link libraries for tests
if(APPLE)
    target_link_libraries(unit.test PRIVATE dl pthread)
else()
    target_link_libraries(unit.test PRIVATE dl pthread rt m)
endif()

set(GTEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/../googletest-src/googletest)

ExternalProject_Get_Property(googletest binary_dir)
set(GTEST_LIBRARY_PATH ${binary_dir}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest.a)
target_link_libraries(unit.test PRIVATE ${GTEST_LIBRARY_PATH})

set(GTEST_MAIN_LIBRARY_PATH ${binary_dir}/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gtest_main.a)
target_link_libraries(unit.test PRIVATE ${GTEST_MAIN_LIBRARY_PATH})

#Include header file paths to the unit tests
target_include_directories(unit.test SYSTEM PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../googletest-src/googletest/include)

#Set specific compiler flags for the unit tests
target_compile_definitions(unit.test PRIVATE -DGTEST_HAS_PTHREAD=1)
target_compile_options(unit.test PRIVATE -Wno-unused-const-variable -Wno-unused-variable)

#Deal with coverage
if(${GCOV})
    add_custom_target(coverage_gcc
            COMMAND cd ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/ && ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test
            COMMAND lcov -o app.info -c --directory ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/ --ignore-errors mismatch --rc geninfo_unexecuted_blocks=1
            COMMAND genhtml app.info -o .
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/coverage/
            DEPENDS unit.test)
endif()

#todo
if(${CLANGCOV})
    add_custom_target(coverage_clang
            COMMAND cd ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/ && ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test
            COMMAND lcov -o app.info -c --directory ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/
            COMMAND genhtml app.info -o .
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/coverage/
            DEPENDS unit.test)
    #add_custom_target(coverage_clang
    #        COMMAND rm -f unit.test.profdata
    #        COMMAND LLVM_PROFILE_FILE=default.profraw ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test
    #        COMMAND llvm-profdata merge -sparse default.profraw -o unit.test.profdata
    #        COMMAND "llvm-cov show -format=html -instr-profile=unit.test.profdata ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test -ignore-filename-regex=\\'.*(vendor|unit)/.*\\' >index.html"
    #        COMMAND "llvm-cov report -instr-profile=unit.test.profdata ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test -ignore-filename-regex='.*(vendor|unit)/.*' -use-color"
    #        COMMAND rm -f default.profraw
    #        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/coverage/
    #        DEPENDS unit.test)
endif()

endif() # NOT WIN32
