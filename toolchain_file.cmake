##
# @file toolchain_file.cmake
# @brief 
#/

include("${CMAKE_CURRENT_LIST_DIR}/compiler_setup.cmake")

if (PLATFORM_BOARD STREQUAL "Raspberry_Pi" AND CONFIG_COMPILE_PREX)
    # Raspberry_Pi build cmake settings
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    # Other platform build cmake settings
    set(CMAKE_SYSTEM_NAME Linux)
endif()

set(CMAKE_C_FLAGS " -g -O1")