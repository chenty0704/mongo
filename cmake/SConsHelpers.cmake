function(import_scons_target cmake_target_name scons_target_name)
    add_custom_target(${cmake_target_name}
            python3 buildscripts/scons.py ${scons_target_name} ${SCONS_OPTIONS} ${SCONS_VARIABLES}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()
