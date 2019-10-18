cmake_minimum_required(VERSION 3.13.0)

project(Mesh CXX C)


SET(CMAKE_BUILD_TYPE "" CACHE STRING "Just making sure the default CMAKE_BUILD_TYPE configurations won't interfere" FORCE)

#Set output folders 
set(CMAKE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/lib)
set(CMAKE_HEADER_OUTPUT_DIRECTORY  ${CMAKE_OUTPUT_DIRECTORY}/include)


#Fetch submodules
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()


#Check if submodules were downloaded
set(submodules
	src/vendor/AlignedMalloc
        src/vendor/gflags
        src/vendor/googletest
        src/vendor/gperftools
        src/vendor/Heap-Layers
        src/vendor/Hoard
        test/atsy/atsy
        )
foreach (submodule ${submodules})
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${submodule}/.git")
        #Split path to submodule folders to get their name
        string(REPLACE "/" ";" submodule_name_list ${submodule})
        list(GET submodule_name_list -1 submodule_name)
        message(FATAL_ERROR "The ${submodule_name} submodule was not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodule and try again.")
    endif()
endforeach()


#Identify compiler and set specific flag
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(GCC TRUE)
    set(CLANG FALSE)
    set(MSVC FALSE)
    set(ICC FALSE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(GCC FALSE)
    set(CLANG TRUE)
    set(MSVC FALSE)
    set(ICC FALSE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(GCC FALSE)
    set(CLANG FALSE)
    set(MSVC TRUE)
    set(ICC FALSE)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    set(BUILD_SHARED_LIBS TRUE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    set(GCC FALSE)
    set(CLANG FALSE)
    set(MSVC FALSE)
    set(ICC TRUE)
endif()

#Create configure options
option(DEBUG "Build with debugging symbols" OFF) #replace with CMAKE_BUILD_TYPE?
option(OPTIMIZE " Build with optimizations" ON) #replace with CMAKE_BUILD_TYPE?
option(GCOV "Build with gcov profiling support" OFF)
option(CLANGCOV "Build with clangcov profiling support" OFF)
set(RANDOMIZATION "1" CACHE STRING "0: no randomization. 1: freelist init only.  2: freelist init + free fastpath")
set_property(CACHE RANDOMIZATION PROPERTY STRINGS "0;1;2")
option(DISABLE_MESHING "Disable meshing" OFF)
option(SUFFIX "Always suffix the mesh library with randomization + meshing info" OFF)
option(CLANG "Build with clang" OFF)

#Parse options
set(CXX_FLAGS )
set(ENV{CXX_FLAGS} )

if (${DEBUG})
    add_definitions(-g)
endif()

if(${OPTIMIZE})
    add_definitions(-O3 -flto -D_FORTIFY_SOURCE=2 -pthread)
else()
    add_definitions(-O0)
endif()

if (${GCOV} AND GCC)
    add_definitions(--coverage)
    link_libraries(gcov)
endif()

if (${CLANGCOV})
    add_definitions(-fprofile-instr-generate -fcoverage-mapping)

    #todo:
    #c.prefer('cc', 'clang')
    #c.prefer('cxx', 'clang++')
    #c.prefer('ar', 'llvm-ar')
    #c.prefer('ranlib', 'llvm-ranlib')
    #c.append('ldflags', '-fuse-ld=lld')
endif()

if (${CLANG})
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_C_COMPILER clang)
endif()

if (NOT ${DISABLE_MESHING})
    set(MESHING_ENABLED 1)
else()
    set(MESHING_ENABLED 0)
endif()

if (${RANDOMIZATION} EQUAL 0)
    set(SHUFFLE_ON_INIT 0)
    set(SHUFFLE_ON_FREE 0)
elseif(${RANDOMIZATION} EQUAL 1)
    set(SHUFFLE_ON_INIT 1)
    set(SHUFFLE_ON_FREE 0)
elseif(${RANDOMIZATION} EQUAL 2)
    set(SHUFFLE_ON_INIT 1)
    set(SHUFFLE_ON_FREE 1)
else()
    message(FATAL_ERROR "Unknown option for Randomization parameter")
endif()

#Create config.h
configure_file(support/config_template.h ${PROJECT_SOURCE_DIR}/src/config.h)

#Additional compile and linking configuration
add_definitions(-fPIC
                -pipe
                -fno-builtin-malloc
                -fno-omit-frame-pointer
                -ffunction-sections
                -fdata-sections
                -Werror=pointer-arith
                -Wall -Wextra -pedantic
                -Werror=return-type
                -Wtype-limits
                -Wempty-body
                -Wvariadic-macros
                -Wcast-align
                -fvisibility=hidden
        )
# c.append('cflags', '-Wno-gnu-statement-expression
# c.append('cflags', '-Wno-c99-extensions

#Language specific definitions
#Quotes for string and backslashes to scape newline.
set(custom_c_flags
     "-Werror=implicit-function-declaration\
      -Werror=implicit-int\
      -Werror=pointer-sign\
     ")
set(custom_cxx_flags
    "-Wno-unused-parameter\
     -Wno-unused-variable\
     -Woverloaded-virtual\
     -Wno-ctor-dtor-privacy\
     -Winvalid-offsetof\
    ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${custom_c_flags}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${custom_cxx_flags}")

if (NOT APPLE AND NOT WIN)
    add_definitions(
            -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700
            -Wundef
        )
    add_link_options(
            -Wl,--no-as-needed
            -Wl,--no-add-needed
            -Wl,--sort-common
            -Wl,--gc-sections
            -Wl,--hash-style=both
            -Wl,--no-undefined
            -Wl,-Bsymbolic-functions
            -Wl,-z,now,-z,relro
            -ftls-model=initial-exec
            -Wl,--exclude-libs,ALL
            -static-libstdc++ -static-libgcc
            -lrt
        )
endif()


add_link_options(-lm -lpthread -pthread -ldl)

set(CMAKE_CXX_STANDARD 14)

#Create folder for coverage
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/coverage)

#Go to subdirectory to build libraries
add_subdirectory(src)


#Todo: deal with installation
