cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/old-panel"
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/pb-app-protocol"
)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

if(DEFINED PB_RUNTIME_PROFILE_DEFAULTS AND
   NOT PB_RUNTIME_PROFILE_DEFAULTS STREQUAL "")
    if(IS_ABSOLUTE "${PB_RUNTIME_PROFILE_DEFAULTS}")
        set(_pb_profile_defaults "${PB_RUNTIME_PROFILE_DEFAULTS}")
    else()
        get_filename_component(
            _pb_profile_defaults
            "${PB_RUNTIME_PROFILE_DEFAULTS}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_LIST_DIR}"
        )
    endif()

    if(NOT EXISTS "${_pb_profile_defaults}")
        message(FATAL_ERROR
            "PB Runtime profile defaults not found: ${_pb_profile_defaults}")
    endif()

    list(APPEND SDKCONFIG_DEFAULTS "${_pb_profile_defaults}")
    message(STATUS "PB Runtime profile defaults: ${_pb_profile_defaults}")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)

project(pb_runtime)
