#Edit configuration files to remove mesh exported configs and varaibles

function(get_rid_of_path bash_config_contents path_to_get_rid bash_contents_updated)
    #Remove libmesh path export
    string(REPLACE "export LIBMESH_PATH=\"${path_to_get_rid}\"\n" "" bash_config_content_lines "${bash_config_contents}")
    #Remove DYLD_FALLBACK export
    string(REPLACE "export DYLD_FALLBACK_LIBRARY_PATH=\"${path_to_get_rid}\"\n" "" bash_config_contents "${bash_config_content_lines}")
    #Save results to the variable from the parent scope
    set(bash_contents_updated "${bash_config_contents}" PARENT_SCOPE)
endfunction(get_rid_of_path)


set(LINUX_BASH_FILE /home/${USER}/.bashrc)
set(MAC_BASH_FILE /Users/${USER}/.bash_profile)

if(SYSWIDE)
    #Install to system/public folders (exports the path to the library and refresh cache on Linux). Mac is weird, the library was completely ignored.
    if(WIN32)
        #Remove exported variables
        execute_process(COMMAND setx /M LIBMESH_PATH=)
    elseif(APPLE)
    else()
        #Linux
        file(REMOVE "/etc/ld.so.conf.d/mesh.conf") #write path to libmesh
        execute_process(COMMAND ldconfig) # renew shared lib cache
    endif()
endif()

#Install to user profile (just exports a variable containing the path to the library)
if(WIN32)
    #Remove exported variable
    execute_process(COMMAND setx LIBMESH_PATH=)
elseif(APPLE)
    #Remove exported variable
    file(COPY ${MAC_BASH_FILE} DESTINATION ${MAC_BASH_FILE}.bak) #backup before doing anything
    file(READ ${MAC_BASH_FILE} bash_contents)
    set(bash_contents_updated)
    get_rid_of_path("${bash_contents}" "${INSTALLATION_DIR}/lib/" bash_contents_updated)
    file(WRITE ${MAC_BASH_FILE} "${bash_contents_updated}")
else()
    #Linux
    #Remove exported variable
    file(COPY ${LINUX_BASH_FILE} DESTINATION ${LINUX_BASH_FILE}.bak) #backup before doing anything
    file(READ ${LINUX_BASH_FILE} bash_contents)
    set(bash_contents_updated)
    get_rid_of_path("${bash_contents}" "${INSTALLATION_DIR}/lib/" bash_contents_updated)
    file(WRITE ${LINUX_BASH_FILE} "${bash_contents_updated}")
endif()
