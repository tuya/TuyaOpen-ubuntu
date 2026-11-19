list_subdirectories(PLATFORM_PUBINC ${PLATFORM_PATH}/tuyaos_adapter)

# Ops contract headers shared by core and the port (core_api_ops.h,
# core_kv_ops.h, core_netif_ops.h, core_network_ops.h, core_bluetooth_ops.h).
list(APPEND PLATFORM_PUBINC "${TOP_SOURCE_DIR}/tuyaopen-core/port/tuyaopen/interface")

# Platform ops cards (core_api_ops_platform, core_network_ops_platform,
# core_netif_ops_platform, core_bluetooth_ops_platform, core_fs_ops_platform)
# are compiled from tuyaopen-core/port/tuyaopen/tuyaopen-core_ops.c by the
# adapter build.
