# have COMPILE_PREX
if (NOT DEFINED CONFIG_COMPILE_PREX)
    set(CONFIG_COMPILE_PREX "")
endif()

# Toolchain path
get_filename_component(TOOLCHAIN_PATH "${CMAKE_CURRENT_LIST_DIR}/../tools/" ABSOLUTE)

# Is cross compile?
if ("${CONFIG_COMPILE_PREX}" STREQUAL "")
    set(IS_CROSS_COMPILE FALSE)
else()
    set(IS_CROSS_COMPILE TRUE)
endif()

if (IS_CROSS_COMPILE)
    set(TOOLCHAIN_PREFIX "")
    if (PLATFORM_BOARD STREQUAL "Raspberry_Pi")
        get_filename_component(TOOLCHAIN_PREFIX "${TOOLCHAIN_PATH}/aarch64-none-linux-gnu-14.2-2024.10/bin" ABSOLUTE)
        # TOOLCHAIN_PREFIX Check
        if(NOT EXISTS "${TOOLCHAIN_PREFIX}")
            message(FATAL_ERROR "Toolchain directory does not exist: ${TOOLCHAIN_PREFIX}")
        endif()

        get_filename_component(TOOLCHAIN_PREFIX "${TOOLCHAIN_PREFIX}/${CONFIG_COMPILE_PREX}" ABSOLUTE)

    endif()

    # On Windows, add .exe suffix to executables
    if(WIN32)
        set(EXE_SUFFIX ".exe")
    else()
        set(EXE_SUFFIX "")
    endif()

    set(CMAKE_C_COMPILER "${TOOLCHAIN_PREFIX}gcc${EXE_SUFFIX}")
    set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}g++${EXE_SUFFIX}")
    set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}gcc${EXE_SUFFIX}")
    set(CMAKE_AR "${TOOLCHAIN_PREFIX}ar${EXE_SUFFIX}")
    set(CMAKE_RANLIB "${TOOLCHAIN_PREFIX}ranlib${EXE_SUFFIX}")
    set(CMAKE_STRIP "${TOOLCHAIN_PREFIX}strip${EXE_SUFFIX}")
else()
    # Native compile settings
    set(CMAKE_C_COMPILER "gcc")
    set(CMAKE_CXX_COMPILER "g++")
    set(CMAKE_ASM_COMPILER "gcc")
    set(CMAKE_AR "ar")
    set(CMAKE_RANLIB "ranlib")
    set(CMAKE_STRIP "strip")
endif()
