cmake_minimum_required(VERSION 3.13.0)

project(Mesh CXX C)


SET(CMAKE_BUILD_TYPE "" CACHE STRING "Just making sure the default CMAKE_BUILD_TYPE configurations won't interfere" FORCE)

#Set output folders
set(CMAKE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY}/lib)
set(CMAKE_HEADER_OUTPUT_DIRECTORY  ${CMAKE_OUTPUT_DIRECTORY}/include)


include(ExternalProject)

ExternalProject_Add(googletest
        GIT_REPOSITORY    https://github.com/google/googletest.git
        GIT_TAG           703bd9caab50b139428cea1aaff9974ebee5742e
        SOURCE_DIR        "${CMAKE_CURRENT_BINARY_DIR}/googletest-src"
        BINARY_DIR        "${CMAKE_CURRENT_BINARY_DIR}/googletest-build"
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
)

ExternalProject_Add(heap_layers
        GIT_REPOSITORY    https://github.com/emeryberger/Heap-Layers.git
        GIT_TAG           a80041cc15174ab82a39bae1cd750b52955c7eef
        SOURCE_DIR        "${CMAKE_CURRENT_BINARY_DIR}/heap_layers-src"
        BINARY_DIR        "${CMAKE_CURRENT_BINARY_DIR}/heap_layers-build"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
)

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
option(INSTALL_MESH "Install mesh to the system" OFF)
option(SYS_WIDE_INSTALL "Install mesh as a system-wide library" OFF)




#Parse options
set(CXX_FLAGS )
set(ENV{CXX_FLAGS} )

if (${CLANG})
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_C_COMPILER clang)
endif()

#Identify compiler and set specific flag
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
    set(GCC TRUE)
    set(CLANGC FALSE)
    set(MSVC FALSE)
    set(ICC FALSE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(GCC FALSE)
    set(CLANGC TRUE)
    set(MSVC FALSE)
    set(ICC FALSE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    set(GCC FALSE)
    set(CLANGC FALSE)
    set(MSVC TRUE)
    set(ICC FALSE)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
    set(BUILD_SHARED_LIBS TRUE)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Intel")
    set(GCC FALSE)
    set(CLANGC FALSE)
    set(MSVC FALSE)
    set(ICC TRUE)
endif()

if (${DEBUG})
    add_definitions(-g)
endif()

if(${OPTIMIZE})
    add_definitions(-O3 -flto -D_FORTIFY_SOURCE=2)
else()
    add_definitions(-O0)
endif()

if (${GCOV} AND GCC)
    find_program(GCOVp gcov)
    if (GCOVp)
        add_definitions(--coverage)
        link_libraries(-lgcov)
    endif()
    find_program(LCOVp lcov)
    if (NOT LCOVp)
        message(WARNING "LCOV is not installed.")
    endif()
endif()

if (${CLANGCOV} AND CLANGC)
    add_definitions(--coverage)#ftest-coverage)
    link_libraries(--coverage)
    #todo:
    #add_definitions(-fprofile-instr-generate -fcoverage-mapping)
    #c.prefer('cc', 'clang')
    #c.prefer('cxx', 'clang++')
    #c.prefer('ar', 'llvm-ar')
    #c.prefer('ranlib', 'llvm-ranlib')
    #c.append('ldflags', '-fuse-ld=lld')
endif()



if (NOT ${DISABLE_MESHING})
    add_definitions(-DMESHING_ENABLED=1)
else()
    add_definitions(-DMESHING_ENABLED=0)
endif()

if (${RANDOMIZATION} EQUAL 0)
    add_definitions(-DSHUFFLE_ON_INIT=0)
    add_definitions(-DSHUFFLE_ON_FREE=0)
elseif(${RANDOMIZATION} EQUAL 1)
    add_definitions(-DSHUFFLE_ON_INIT=1)
    add_definitions(-DSHUFFLE_ON_FREE=0)
elseif(${RANDOMIZATION} EQUAL 2)
    add_definitions(-DSHUFFLE_ON_INIT=1)
    add_definitions(-DSHUFFLE_ON_FREE=1)
else()
    message(FATAL_ERROR "Unknown option for Randomization parameter")
endif()

#Additional compile and linking configuration
add_definitions(
        -fPIC
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
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${custom_c_flags}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${custom_cxx_flags}")

if (NOT APPLE AND GCC)
    add_definitions(
            -Wno-unused-parameter
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

set(CMAKE_CXX_STANDARD 14)

#Create folder for coverage
file(MAKE_DIRECTORY ${PROJECT_SOURCE_DIR}/coverage)

#Go to subdirectory to build libraries
add_subdirectory(src)

#Installation procedures
if(INSTALL_MESH)
    #Check OS and target installation directories
    if(WIN32)
        set(system_wide_installation_dir C:\\Program Files\\mesh)
        set(user_wide_installation_dir C:\\User\\$ENV{USERNAME}\\AppData\\Local\\mesh)
        set(user $ENV{USERNAME})
        add_custom_target(delete_local_installation_dir
                COMMAND ${CMAKE_COMMAND} -E remove_directory ${user_wide_installation_dir})
    elseif(APPLE)
        set(system_wide_installation_dir /usr/local)
        set(user_wide_installation_dir /Users/$ENV{USER}/Library/mesh)
        set(user $ENV{USER})
        add_custom_target(delete_local_installation_dir
                COMMAND ${CMAKE_COMMAND} -E remove_directory ${user_wide_installation_dir})
    else()
        #Linux
        set(system_wide_installation_dir /usr/local)
        set(user_wide_installation_dir /home/$ENV{USERNAME}/.local)
        set(user $ENV{USERNAME})
        add_custom_target(delete_local_installation_dir)
    endif()

    #This check prevents that running installation as sudo changes the username
    if(NOT ENV{mesh_install_dir})
        if(SYS_WIDE_INSTALL)
            set(ENV{mesh_install_dir} ${system_wide_installation_dir})
            set(ENV{mesh_syswide} ON)
        else()
            set(ENV{mesh_install_dir} ${user_wide_installation_dir})
            set(ENV{mesh_syswide} OFF)
        endif()
        set(ENV{mesh_user} ${user})
    endif()

    #Create target for copying library and header dir
    add_custom_target(install_mesh
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mesh> $ENV{mesh_install_dir}/lib/$<TARGET_FILE_NAME:mesh>
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/plasma/mesh.h $ENV{mesh_install_dir}/include/plasma/mesh.h
            )
    #Create target for deleting library and header dir
    add_custom_target(uninstall_mesh
            COMMAND ${CMAKE_COMMAND} -E remove -f $ENV{mesh_install_dir}/lib/$<TARGET_FILE_NAME:mesh>
            COMMAND ${CMAKE_COMMAND} -E remove -f $ENV{mesh_install_dir}/include/plasma/mesh.h
            COMMAND ${CMAKE_COMMAND} -E remove_directory $ENV{mesh_install_dir}/include/plasma
            )

    #Export or remove library paths (you will need to run with sudo/elevated privileges if doing a system wide installation)
    #I didn't add a DEPENDS clause on purpose, or the mesh and install_mesh targets would run as root
    add_custom_target(export_mesh
            COMMAND ${CMAKE_COMMAND} -DINSTALLATION_DIR="$ENV{mesh_install_dir}" -DUSER="$ENV{mesh_user}" -DSYSWIDE="$ENV{mesh_syswide}" -P ${CMAKE_SOURCE_DIR}/support/export_mesh.cmake
            )
    add_custom_target(remove_export_mesh
            COMMAND ${CMAKE_COMMAND} -DINSTALLATION_DIR="$ENV{mesh_install_dir}" -DUSER="$ENV{mesh_user}" -DSYSWIDE="$ENV{mesh_syswide}" -P ${CMAKE_SOURCE_DIR}/support/remove_export_mesh.cmake
            )

    #Additional glue targets that trigger both install&export or uninstall&remove export
    add_custom_target(install_and_export
            DEPENDS install_mesh export_mesh
            )

    if(SYS_WIDE_INSTALL)
        add_custom_target(uninstall_and_remove_export
                DEPENDS uninstall_mesh remove_export_mesh
                )
    else()
        add_custom_target(uninstall_and_remove_export
                DEPENDS uninstall_mesh remove_export_mesh delete_local_installation_dir
                )
    endif()

endif()
