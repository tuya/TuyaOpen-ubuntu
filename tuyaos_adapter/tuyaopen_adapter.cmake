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

set(SOURCES "")

# Initialize global variable to collect found libraries
set(TUYAOPEN_FOUND_LIBRARIES "")

# Function to find and collect library
# Argument 1: library_path - Path to the library directory
# Argument 2: library_name - Name of the library
function(collect_library library_path library_name)
    set(LIB_EXTENSIONS .so .a .dylib .lib)
    
    # Search for library with different prefixes and extensions
    foreach(prefix "" "lib")
        foreach(ext ${LIB_EXTENSIONS})
            set(full_path "${library_path}/${prefix}${library_name}${ext}")
            if(EXISTS ${full_path})
                message(STATUS "Found library: ${full_path}")
                link_directories(${library_path})
                list(APPEND TUYAOPEN_FOUND_LIBRARIES ${library_name})
                set(TUYAOPEN_FOUND_LIBRARIES ${TUYAOPEN_FOUND_LIBRARIES} PARENT_SCOPE)
                return()
            endif()
        endforeach()
    endforeach()
    
    message(WARNING "Library not found: ${library_name} (path: ${library_path})")
endfunction()

# common files
file(GLOB_RECURSE UTILITIES_SRC "${CMAKE_CURRENT_LIST_DIR}/include/utilities/*.c")
file(GLOB ADAPTER_SRC "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
set(INIT_SRC "${CMAKE_CURRENT_LIST_DIR}/include/init/src/tkl_init_common.c")
list(APPEND SOURCES 
    ${UTILITIES_SRC} 
    ${ADAPTER_SRC} 
    ${INIT_SRC}
)
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/include/adc
    ${CMAKE_CURRENT_LIST_DIR}/include/flash
    ${CMAKE_CURRENT_LIST_DIR}/include/gpio
    ${CMAKE_CURRENT_LIST_DIR}/include/i2c
    ${CMAKE_CURRENT_LIST_DIR}/include/init/include
    ${CMAKE_CURRENT_LIST_DIR}/include/network
    ${CMAKE_CURRENT_LIST_DIR}/include/pinmux
    ${CMAKE_CURRENT_LIST_DIR}/include/pwm
    ${CMAKE_CURRENT_LIST_DIR}/include/rtc
    ${CMAKE_CURRENT_LIST_DIR}/include/security
    ${CMAKE_CURRENT_LIST_DIR}/include/spi
    ${CMAKE_CURRENT_LIST_DIR}/include/system
    ${CMAKE_CURRENT_LIST_DIR}/include/uart
    ${CMAKE_CURRENT_LIST_DIR}/include/utilities/include
    ${CMAKE_CURRENT_LIST_DIR}/include/watchdog
)

# gpio files
if(CONFIG_ENABLE_GPIO STREQUAL "y")
    list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/tkl_gpio.c")
endif()

# i2c files
if(CONFIG_ENABLE_I2C STREQUAL "y")
    list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/tkl_i2c.c")
endif()

# spi files
if(CONFIG_ENABLE_SPI STREQUAL "y")
    list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/tkl_spi.c")
endif()

# pwm files
if(CONFIG_ENABLE_PWM STREQUAL "y")
    list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/tkl_pwm.c")
endif()

# uart files
if(CONFIG_ENABLE_UART STREQUAL "y")
    list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/tkl_uart.c")
endif()

# wire files
if(CONFIG_ENABLE_WIRED)
    list(APPEND SOURCES 
            "${CMAKE_CURRENT_LIST_DIR}/include/init/src/tkl_init_wired.c"
    )
    include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/include/wired
    )
endif()

# wifi files
if(CONFIG_ENABLE_WIFI)
    file(GLOB_RECURSE WIFI_SRC "${CMAKE_CURRENT_LIST_DIR}/src/tkl_wifi/*.c")
    list(APPEND INIT_SRC "${CMAKE_CURRENT_LIST_DIR}/include/init/src/tkl_init_wifi.c")
    list(APPEND WPA_INC
        "${CMAKE_CURRENT_LIST_DIR}/src/tkl_wifi/iwlib"
        "${CMAKE_CURRENT_LIST_DIR}/src/tkl_wifi/wpa_s"
        "${CMAKE_CURRENT_LIST_DIR}/src/tkl_wifi/wpa_cli"
        "${CMAKE_CURRENT_LIST_DIR}/src/tkl_wifi/log"
    )

    list(APPEND SOURCES ${WIFI_SRC})
    include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/include/wifi
        ${WPA_INC}
    )
endif()

# bluetooth files
if(CONFIG_ENABLE_BLUETOOTH)
    file(GLOB_RECURSE BT_SRC "${CMAKE_CURRENT_LIST_DIR}/src/tkl_bt/*.c")
    set(BT_INC)
    list(APPEND BT_INC
        "${CMAKE_CURRENT_LIST_DIR}/include/bluetooth"
        "${CMAKE_CURRENT_LIST_DIR}/include/hci"
        "${CMAKE_CURRENT_LIST_DIR}/src/tkl_bt/include"
    )

    list(APPEND SOURCES ${BT_SRC})
    include_directories(${BT_INC})
endif()

# audio files
if(CONFIG_ENABLE_AUDIO)
    file(GLOB_RECURSE AUDIO_SRC "${CMAKE_CURRENT_LIST_DIR}/src/tkl_audio/*.c")
    list(APPEND SOURCES ${AUDIO_SRC})
    include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/include/media
    )

    # audio library
    set(AUDIO_LIB_PATH "${CMAKE_CURRENT_LIST_DIR}/src/tkl_audio/libs/${PLATFORM_CHIP}")
    if (NOT EXISTS "${AUDIO_LIB_PATH}")
        message(FATAL_ERROR "Audio library path does not exist: ${AUDIO_LIB_PATH}")
    endif()
    
    ## audio subsystem library
    collect_library("${AUDIO_LIB_PATH}/audio_subsys" "audio_subsys")
    include_directories("${AUDIO_LIB_PATH}/audio_subsys/include")

    ## MNN library
    collect_library("${AUDIO_LIB_PATH}/MNN" "MNN")
    include_directories("${AUDIO_LIB_PATH}/MNN/include")

    ## audio codec library
    collect_library("${AUDIO_LIB_PATH}/opus" "opus")
    include_directories("${AUDIO_LIB_PATH}/opus/include")

    ## alsa library (for Raspberry Pi)
    if (IS_CROSS_COMPILE)
        collect_library("${AUDIO_LIB_PATH}/alsa" "asound")
        include_directories("${AUDIO_LIB_PATH}/alsa/include")
    else()
        # For native builds, use system ALSA library
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(ALSA alsa QUIET)
        endif()
        
        if(NOT ALSA_FOUND)
            find_library(ALSA_LIBRARY NAMES asound)
            find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
            if(ALSA_LIBRARY AND ALSA_INCLUDE_DIR)
                set(ALSA_FOUND TRUE)
                set(ALSA_INCLUDE_DIRS ${ALSA_INCLUDE_DIR})
            endif()
        endif()
        
        if(ALSA_FOUND)
            list(APPEND TUYAOPEN_FOUND_LIBRARIES asound)
            if(ALSA_INCLUDE_DIRS)
                include_directories(${ALSA_INCLUDE_DIRS})
            endif()
        else()
            message(FATAL_ERROR "ALSA library not found. Install: sudo apt install libasound2-dev")
        endif()
    endif()
endif()

