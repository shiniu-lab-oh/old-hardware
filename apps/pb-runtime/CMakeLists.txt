cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/old-panel"
    "${CMAKE_CURRENT_LIST_DIR}/../../sdk/pb-app-protocol"
)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(pb_runtime)
