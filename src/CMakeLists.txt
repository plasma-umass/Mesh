
#Include current folder to guarantee headers are found by files
include_directories(./)
include_directories(./vendor/Heap-Layers)
include_directories(./vendor/AlignedMalloc)

#Create a set of sources for cross platform aligned malloc
set(aligned_malloc_src
	vendor/AlignedMalloc/aligned_free.c
	vendor/AlignedMalloc/aligned_malloc.c
	)


#Add a target to fragmenter executable
add_executable(fragmenter fragmenter.cc measure_rss.cc)

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
#Add a target to the meshing benchmark executable
add_executable(meshing-benchmark meshing_benchmark.cc ${common_src})

#Create the set of source files for the mesh library
set(mesh_src
        ${common_src}
        libmesh.cc
        )
#Add a target for the mesh shared library
add_library(mesh SHARED ${mesh_src})


#Create a set of source files from the google test suite for the unit tests
set(google_src
        vendor/googletest/googletest/src/gtest-all.cc
        vendor/googletest/googletest/src/gtest_main.cc
        )

#Add definition required by the Google tests
if (WIN)
    add_definitions(-DGTEST_OS_WINDOWS)
elseif(APPLE)
    add_definitions(-DGTEST_OS_MAC)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DGTEST_OS_LINUX)
else()
    message(FATAL_ERROR Unsupported platform)
endif()

#Create a set of source files for the unit tests
set(unit_src
        ${common_src}
        ${google_src}
        unit/alignment.cc
        unit/binned_tracker_test.cc
        unit/bitmap_test.cc
        unit/concurrent_mesh_test.cc
        unit/mesh_test.cc
        unit/rng_test.cc
        unit/size_class_test.cc
        unit/triple_mesh_test.cc
        )
#Add a target to the unit tests executable
add_executable(unit.test ${unit_src})

#Include header file paths to the unit tests
target_include_directories(unit.test SYSTEM PRIVATE vendor/googletest/googletest/include)
target_include_directories(unit.test PRIVATE vendor/googletest/googletest)

#Set specific compiler flags for the unit tests
target_compile_definitions(unit.test PRIVATE -DGTEST_HAS_PTHREAD=1)
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

set(test_filenames
        big-alloc.c
        fork-example.c
        global-large-stress.cc
        larson.cc
        local-alloc.c
        thread.cc
    )

set (libraries_to_link
        mesh
        )

foreach(testfile ${test_filenames})
    string(REPLACE "." ";" filename_list ${testfile})
    list(GET filename_list 0 testname)
    add_mesh_test(${testname} ${testfile} "${libraries_to_link}")
endforeach()

#Create additional larson that relies on GCC allocator
add_mesh_test(larson-glibc larson.cc "")

#todo: build and link tcmalloc
#Build tcmalloc
#set(tcmalloc_src
#        vendor/gperftools/src/
#        )
#add_library(tcmalloc_minimal SHARED ${tcmalloc_src})

#Create additional larson that relies on tcmalloc_minimal
#add_test(larson-tcmalloc larson.cc tcmalloc_minimal)

if(UNIX AND NOT APPLE)
#Build hoard
set(hoard_src
        vendor/Hoard/src/source/libhoard.cpp
        vendor/Hoard/src/source/uselibhoard.cpp
        )
if(WIN)
    list(APPEND hoard_src vendor/Hoard/src/source/wintls.cpp)
elseif(APPLE)
    list(APPEND hoard_src vendor/Hoard/src/source/mactls.cpp)
else()
    list(APPEND hoard_src vendor/Hoard/src/source/unixtls.cpp)
endif()
include_directories(vendor/Hoard/src/include)
include_directories(vendor/Hoard/src/include/hoard)
include_directories(vendor/Hoard/src/include/superblocks)
include_directories(vendor/Hoard/src/include/util)
add_library(hoard SHARED ${hoard_src})

#Create additional tests that require hoard
add_mesh_test(big-alloc-hoard big-alloc.c hoard)
add_mesh_test(larson-hoard larson.cc hoard)
endif()

#Deal with coverage
if(${GCOV})
    target_compile_options(unit.test PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(unit.test PRIVATE -fprofile-arcs -ftest-coverage)
    add_custom_target(coverage
            COMMAND cd ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/ && ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unit.test
            COMMAND lcov -o app.info -c --directory ${CMAKE_BINARY_DIR}/src/CMakeFiles/unit.test.dir/
            COMMAND genhtml app.info
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/coverage/
            DEPENDS unit.test)
endif()

#todo
#if(${CLANGCOV})
#endif()

