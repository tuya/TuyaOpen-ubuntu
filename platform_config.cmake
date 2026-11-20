list_subdirectories(PLATFORM_PUBINC ${PLATFORM_PATH}/tuyaos_adapter)

# This platform implements the TKL file system API used by core's FS ops card.
add_definitions(-DTUYAOPEN_HAS_TKL_FS=1)

# Platform ops cards (core_api_ops_platform, core_network_ops_platform,
# core_netif_ops_platform, core_bluetooth_ops_platform, core_fs_ops_platform)
# are compiled from tuyaopen-core/port/tuyaopen/tuyaopen-core_ops.c by the
# adapter build.
