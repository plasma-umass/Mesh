#Include current folder to guarantee headers are found by files
include_directories(./)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/../heap_layers-src)
# include_directories(./vendor/Heap-Layers)

#Create a common set of source files
set(common_src
        d_assert.cc
        global_heap.cc
        runtime.cc
        real.cc
        meshable_arena.cc
        measure_rss.cc
        thread_local_heap.cc
        )

#Create the set of source files for the mesh library
set(mesh_src
        ${common_src}
        libmesh.cc
        )
#Add a target for the mesh shared library
add_library(mesh SHARED ${mesh_src})
target_link_libraries(mesh PRIVATE -pthread -ldl)

#Create a set of source files for the unit tests
set(unit_src
        ${common_src}
        ${google_src}
        testing/unit/alignment.cc
        testing/unit/bitmap_test.cc
        testing/unit/concurrent_mesh_test.cc
        testing/unit/mesh_test.cc
        testing/unit/rng_test.cc
        testing/unit/size_class_test.cc
        testing/unit/triple_mesh_test.cc
)
#Add a target to the unit tests executable
add_executable(unit.test ${unit_src})
target_link_libraries(unit.test PRIVATE -ldl -pthread )

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
            COMMAND lcov -o app.info -c --directory ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/
            COMMAND genhtml app.info
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/coverage/
            DEPENDS unit.test)
endif()

#todo
if(${CLANGCOV})
    add_custom_target(coverage_clang
            COMMAND cd ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/ && ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test
            COMMAND lcov -o app.info -c --directory ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/
            COMMAND genhtml app.info
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
