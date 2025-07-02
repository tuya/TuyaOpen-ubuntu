##
# @file toolchain_file.cmake
# @brief 
#/

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

# -fsanitize=address -fno-omit-frame-pointer
set(CMAKE_C_FLAGS " -g")
