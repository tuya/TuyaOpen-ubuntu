##
# @file toolchain_file.cmake
# @brief 
#/

# Toolchain path
set(TOOLCHAIN_PATH "${PLATFORM_PATH}/../tools/")
# Compile path
set(COMPILE_PATH "")

# If CONFIG_COMPILE_PREX is defined, use cross compile prefix
if(DEFINED CONFIG_COMPILE_PREX AND NOT "${CONFIG_COMPILE_PREX}" STREQUAL "")
    message(STATUS "Using cross compile prefix: ${CONFIG_COMPILE_PREX}")

    if("${CONFIG_COMPILE_PREX}" MATCHES "^aarch64-none-linux-gnu-")
        set(TOOLCHAIN_DIR "${PLATFORM_PATH}/../tools/aarch64-none-linux-gnu-10.3-2021.07")
        set(COMPILE_PATH "${TOOLCHAIN_DIR}/bin/${CONFIG_COMPILE_PREX}")
    else()
        set(COMPILE_PATH "${CONFIG_COMPILE_PREX}")
    endif()
    
    # On Windows, add .exe suffix to executables
    if(WIN32)
        set(EXE_SUFFIX ".exe")
    else()
        set(EXE_SUFFIX "")
    endif()
    
    set(CMAKE_C_COMPILER ${COMPILE_PATH}gcc${EXE_SUFFIX})
    set(CMAKE_CXX_COMPILER ${COMPILE_PATH}g++${EXE_SUFFIX})
    set(CMAKE_ASM_COMPILER ${COMPILE_PATH}gcc${EXE_SUFFIX})
    set(CMAKE_AR ${COMPILE_PATH}ar${EXE_SUFFIX})
    set(CMAKE_RANLIB ${COMPILE_PATH}ranlib${EXE_SUFFIX})
    set(CMAKE_STRIP ${COMPILE_PATH}strip${EXE_SUFFIX})
    
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
