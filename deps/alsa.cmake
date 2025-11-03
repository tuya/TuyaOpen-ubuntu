# ALSA Dependency Detection
# This module handles finding and linking the ALSA library
# Works for both native and cross-compilation builds

if(CONFIG_ENABLE_AUDIO_ALSA STREQUAL "y")
    message(STATUS "Checking for ALSA library...")
    
    # For cross-compilation, respect CMAKE_FIND_ROOT_PATH
    # For native builds, use system paths
    
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        # For cross-compilation, pkg-config needs PKG_CONFIG_LIBDIR
        # Set it based on CMAKE_FIND_ROOT_PATH if cross-compiling
        if(DEFINED CMAKE_FIND_ROOT_PATH AND NOT "${CMAKE_FIND_ROOT_PATH}" STREQUAL "")
            # Try pkg-config from sysroot first
            set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_FIND_ROOT_PATH}/usr/lib/pkgconfig:${CMAKE_FIND_ROOT_PATH}/usr/share/pkgconfig")
            message(STATUS "  Using sysroot for pkg-config: ${CMAKE_FIND_ROOT_PATH}")
        endif()
        pkg_check_modules(ALSA alsa QUIET)
        unset(ENV{PKG_CONFIG_LIBDIR})  # Clean up
    endif()
    # Fallback: try to find ALSA library directly
    # This respects CMAKE_FIND_ROOT_PATH automatically
    if(NOT ALSA_FOUND)
        # First try sysroot paths (for cross-compilation)
        if(DEFINED CMAKE_FIND_ROOT_PATH AND NOT "${CMAKE_FIND_ROOT_PATH}" STREQUAL "")
            find_library(ALSA_LIBRARY 
                NAMES asound
                PATHS ${CMAKE_FIND_ROOT_PATH}
                PATH_SUFFIXES lib lib64 usr/lib usr/lib64
                NO_DEFAULT_PATH
            )
            find_path(ALSA_INCLUDE_DIR 
                NAMES alsa/asoundlib.h
                PATHS ${CMAKE_FIND_ROOT_PATH}
                PATH_SUFFIXES include usr/include
                NO_DEFAULT_PATH
            )
        endif()
        
        # Also search system paths if not found in sysroot
        if(NOT ALSA_LIBRARY)
            find_library(ALSA_LIBRARY NAMES asound)
        endif()
        if(NOT ALSA_INCLUDE_DIR)
            find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
        endif()
        
        if(ALSA_LIBRARY AND ALSA_INCLUDE_DIR)
            set(ALSA_FOUND TRUE)
            set(ALSA_LIBRARIES ${ALSA_LIBRARY})
            set(ALSA_INCLUDE_DIRS ${ALSA_INCLUDE_DIR})
        endif()
    endif()
    
    if(ALSA_FOUND)
        target_link_libraries(${CONFIG_PROJECT_NAME} ${ALSA_LIBRARIES})
        if(ALSA_INCLUDE_DIRS)
            target_include_directories(${CONFIG_PROJECT_NAME} PRIVATE ${ALSA_INCLUDE_DIRS})
        endif()
        message(STATUS "  ALSA library found and linked")
        if(DEFINED CMAKE_FIND_ROOT_PATH AND NOT "${CMAKE_FIND_ROOT_PATH}" STREQUAL "")
            message(STATUS "    Found in sysroot: ${CMAKE_FIND_ROOT_PATH}")
            message(STATUS "    Library: ${ALSA_LIBRARIES}")
            message(STATUS "    Include: ${ALSA_INCLUDE_DIRS}")
        else()
            message(STATUS "    Found in system paths")
        endif()
    else()
        if(DEFINED CMAKE_FIND_ROOT_PATH AND NOT "${CMAKE_FIND_ROOT_PATH}" STREQUAL "")
            message(FATAL_ERROR 
                "\n"
                "===================================================================\n"
                "ALSA library not found in sysroot: ${CMAKE_FIND_ROOT_PATH}\n"
                "===================================================================\n"
                "For cross-compilation, install ALSA development files in your sysroot:\n"
                "\n"
                "Ubuntu/Debian:\n"
                "  1. Install packages in sysroot:\n"
                "     sudo debootstrap --arch=<arch> <release> <sysroot_path>\n"
                "     sudo chroot <sysroot_path> apt-get install libasound2-dev\n"
                "\n"
                "  2. Or copy ALSA files manually to:\n"
                "   ${CMAKE_FIND_ROOT_PATH}/usr/lib/\n"
                "     ${CMAKE_FIND_ROOT_PATH}/usr/include/\n"
                "\n"
                "  3. Ensure CMAKE_FIND_ROOT_PATH points to your sysroot\n"
                "===================================================================")
        else()
            message(FATAL_ERROR 
                "\n"
                "===================================================================\n"
                "ALSA library (libasound2-dev) not found.\n"
                "===================================================================\n"
                "Please install the ALSA development package for your distribution:\n"
                "\n"
                "  Ubuntu/Debian:  sudo apt-get install libasound2-dev\n"
                "  Fedora/RHEL:    sudo dnf install alsa-lib-devel\n"
                "  Arch Linux:     sudo pacman -S alsa-lib\n"
                "  Alpine:         sudo apk add alsa-lib-dev\n"
                "\n"
                "Or see DEPENDENCIES.md for more information.\n"
                "===================================================================")
        endif()
    endif()
endif()

