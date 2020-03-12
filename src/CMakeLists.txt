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


#Create a set of source files from the google test suite for the unit tests
set(google_src
#        ${CMAKE_CURRENT_BINARY_DIR}/googletest-src/src/gtest-all.cc
#        ${CMAKE_CURRENT_BINARY_DIR}/googletest-src/src/gtest_main.cc
        )

#Add definition required by the Google tests (to make it stop complaining)
if (WIN32)
    set(win_def -DGTEST_OS_MAC=0
                -DGTEST_OS_LINUX=0
                -DGTEST_OS_WINDOWS
            )
elseif(APPLE)
    set(apple_def -DGTEST_OS_MAC
                  -DGTEST_OS_LINUX=0
                  -DGTEST_OS_WINDOWS=0
            )
elseif(UNIX AND NOT APPLE)
    set(linux_def -DGTEST_OS_MAC=0
                  -DGTEST_OS_LINUX
                  -DGTEST_OS_WINDOWS=0
            )
else()
    message(FATAL_ERROR Unsupported platform)
endif()
set(google_tests_definitions
        -DGTEST_FOR_GOOGLE_=0
        -DGTEST_HAS_ABSL=0
        -DGTEST_IS_THREADSAFE=1
        -DGTEST_OS_AIX=0
        -DGTEST_OS_FUCHSIA=0
        -DGTEST_OS_IOS=0
        ${linux_def}
        -DGTEST_OS_LINUX_ANDROID=0
        ${apple_def}
        -DGTEST_OS_NACL=0
        -DGTEST_OS_OS2=0
        -DGTEST_OS_QNX=0
        -DGTEST_OS_SYMBIAN=0
        -DGTEST_OS_USES_PCRE=0
        ${win_def}
        -DGTEST_OS_WINDOWS_MOBILE=0
        -DGTEST_OS_WINDOWS_PHONE=0
        -DGTEST_OS_WINDOWS_RT=0
        -DGTEST_OS_ZOS=0
        -DGTEST_USES_PCRE=0
        -DGTEST_USES_SIMPLE_RE=0
)


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

#Include header file paths to the unit tests
target_include_directories(unit.test SYSTEM PRIVATE vendor/googletest/googletest/include)
target_include_directories(unit.test PRIVATE vendor/googletest/googletest)

#Set specific compiler flags for the unit tests
target_compile_definitions(unit.test PRIVATE -DGTEST_HAS_PTHREAD=1 ${google_tests_definitions})
target_compile_options(unit.test PRIVATE -Wno-unused-const-variable)

#Create targets for each test
function (add_mesh_test testname testfilename libraries_to_link)
    add_executable(${testname} test/${testfilename} ${aligned_malloc_src})
    list(LENGTH libraries_to_link length)
    if (length GREATER 0)
        target_link_libraries(${testname} "${libraries_to_link}")
    endif()
    target_compile_options(${testname} PRIVATE -pipe -fno-builtin-malloc -fno-omit-frame-pointer)

    #Include target to run the executable
    add_custom_target(run_${testname}
            COMMAND ${testname}
            WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            DEPENDS ${testname})
endfunction(add_mesh_test)

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
