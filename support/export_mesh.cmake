#Edit configuration files to export mesh folder and auxiliary variables

set(LINUX_BASH_FILE /home/${USER}/.bashrc)
set(MAC_BASH_FILE /Users/${USER}/.bash_profile)

if(SYSWIDE)
    #Install to system/public folders (exports the path to the library and refresh cache on Linux). Mac is weird, the library was completely ignored.
    if(WIN32)
        execute_process(COMMAND setx /M LIBMESH_PATH "${INSTALLATION_DIR}\\lib\\")
    elseif(APPLE)
        file(APPEND ${MAC_BASH_FILE} "export DYLD_FALLBACK_LIBRARY_PATH=\"${INSTALLATION_DIR}/lib/\"\nexport LIBMESH_PATH=\"${INSTALLATION_DIR}/lib/\"\n")
    else()
        #Linux
        file(WRITE "/etc/ld.so.conf.d/mesh.conf" "${INSTALLATION_DIR}/lib") #write path to libmesh
        execute_process(COMMAND ldconfig) # renew shared lib cache
        file(APPEND ${LINUX_BASH_FILE} "export LIBMESH_PATH=\"${INSTALLATION_DIR}/lib/\"\n") #not really necessary, but just to make things consistent
    endif()
else()
    #Install to user profile (just exports a variable containing the path to the library)
    if(WIN32)
        execute_process(COMMAND setx LIBMESH_PATH "${INSTALLATION_DIR}\\lib\\")
    elseif(APPLE)
        file(APPEND ${MAC_BASH_FILE} "export LIBMESH_PATH=\"${INSTALLATION_DIR}/lib/\"\n")
    else()
        #Linux
        file(APPEND ${LINUX_BASH_FILE} "export LIBMESH_PATH=\"${INSTALLATION_DIR}/lib/\"\n")
    endif()
endif()