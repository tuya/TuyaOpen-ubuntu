cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED BUILD_PARAM_PATH)
    message(FATAL_ERROR "BUILD_PARAM_PATH is not defined, using default value")
endif()

if(EXISTS ${BUILD_PARAM_PATH}/build_param.cmake)
    include(${BUILD_PARAM_PATH}/build_param.cmake)
else()
    message(FATAL_ERROR "${BUILD_PARAM_PATH}/build_param.cmake not found")
endif()

if(DEFINED CONFIG_COMPILE_PREX AND NOT "${CONFIG_COMPILE_PREX}" STREQUAL "")
    message(STATUS "Using cross compile prefix: ${CONFIG_COMPILE_PREX}")
    
    set(CMAKE_C_COMPILER ${CONFIG_COMPILE_PREX}gcc)
    set(CMAKE_CXX_COMPILER ${CONFIG_COMPILE_PREX}g++)
    set(CMAKE_ASM_COMPILER ${CONFIG_COMPILE_PREX}gcc)
    set(CMAKE_AR ${CONFIG_COMPILE_PREX}ar)
    set(CMAKE_RANLIB ${CONFIG_COMPILE_PREX}ranlib)
    set(CMAKE_STRIP ${CONFIG_COMPILE_PREX}strip)
    
    set(CMAKE_SYSTEM_NAME Linux)
    
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
else()
    message(STATUS "Using native compilation (CONFIG_COMPILE_PREX not set or empty)")
endif()

project(TuyaOpen)


if(CONFIG_ENABLE_WIFI STREQUAL "y")
    set(CONFIG_ENABLE_WIFI ON)
else()
    set(CONFIG_ENABLE_WIFI OFF)
endif()

if(CONFIG_ENABLE_BLUETOOTH STREQUAL "y")
    set(CONFIG_ENABLE_BLUETOOTH ON)
else()
    set(CONFIG_ENABLE_BLUETOOTH OFF)
endif()

if(CONFIG_ENABLE_WIRED STREQUAL "y")
    set(CONFIG_ENABLE_WIRED ON)
else()
    set(CONFIG_ENABLE_WIRED OFF)
endif()

file(GLOB_RECURSE UTILITIES_SRC "tuyaos_adapter/include/utilities/*.c")
file(GLOB ADAPTER_SRC "tuyaos_adapter/src/*.c")
set(INIT_SRC "tuyaos_adapter/include/init/src/tkl_init_common.c")

if(CONFIG_ENABLE_WIFI)
    file(GLOB_RECURSE WIFI_SRC "tuyaos_adapter/src/tkl_wifi/*.c")
    list(APPEND INIT_SRC "tuyaos_adapter/include/init/src/tkl_init_wifi.c")
endif()

if(CONFIG_ENABLE_BLUETOOTH)
    file(GLOB_RECURSE BT_SRC "tuyaos_adapter/src/tkl_bt/*.c")
endif()

if(CONFIG_ENABLE_WIRED)
    list(APPEND INIT_SRC "tuyaos_adapter/include/init/src/tkl_init_wired.c")
endif()

set(SOURCES 
    ${UTILITIES_SRC} 
    ${ADAPTER_SRC} 
    ${INIT_SRC}
    ${WIFI_SRC}
    ${BT_SRC}
)

file(GLOB_RECURSE ADAPTER_INCLUDE_SUB_DIRS LIST_DIRECTORIES true "tuyaos_adapter/include/*")
list(FILTER ADAPTER_INCLUDE_SUB_DIRS INCLUDE REGEX ".*/$")
foreach(dir ${ADAPTER_INCLUDE_SUB_DIRS})
    if(IS_DIRECTORY ${dir})
        include_directories(${dir})
    endif()
endforeach()

include_directories(
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/adc
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/bluetooth
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/flash
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/gpio
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/hci
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/i2c
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/init/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/network
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/pinmux
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/pwm
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/rtc
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/security
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/system
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/timer
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/uart
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/utilities/include
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/watchdog
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/wifi
    ${CMAKE_SOURCE_DIR}/tuyaos_adapter/include/wired
)

if(OPEN_HEADER_DIR)
    string(REPLACE ";" " " HEADER_DIR_LIST "${OPEN_HEADER_DIR}")
    separate_arguments(HEADER_DIR_LIST)
    foreach(header_dir ${HEADER_DIR_LIST})
        include_directories(${header_dir})
    endforeach()
endif()

add_definitions(-g)
add_compile_options(-fdiagnostics-color=always) 

add_executable(${CONFIG_PROJECT_NAME} ${SOURCES})

target_link_libraries(${CONFIG_PROJECT_NAME}
    -L${OPEN_LIBS_DIR}
    pthread
    m
)

if(NOT DEFINED CONFIG_COMPILE_PREX OR "${CONFIG_COMPILE_PREX}" STREQUAL "")
    target_link_libraries(${CONFIG_PROJECT_NAME}
        systemd
    )
endif()

if(PLATFORM_NEED_LIBS)
    string(REPLACE " " ";" LIBS_LIST "${PLATFORM_NEED_LIBS}")
    target_link_libraries(${CONFIG_PROJECT_NAME}
        ${LIBS_LIST}
    )
endif()
