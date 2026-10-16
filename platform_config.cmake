list_subdirectories(PLATFORM_PUBINC ${PLATFORM_PATH}/tuyaos_adapter)

# [tuyaopen-core integration] 契约层（C2，与 dev 树 linux/esp32 一致）：core 与平台
# 共用的 ops 类型（tal_api_ops.h / tal_kv_ops.h / tal_netif_ops.h /
# tal_network_ops.h / tal_bluetooth_ops.h + tal_bluetooth_def.h）物理放在本平台
# tuyaos_adapter/tuyaopen/interface/；core 的 tal_*.h 反向 include 它们（经
# PLATFORM_PUBINC → tuyaopen-headers 注入 core 编译单元），平台侧编译单元由
# tuyaopen_adapter.cmake 直接 include 同目录。
list(APPEND PLATFORM_PUBINC "${CMAKE_CURRENT_LIST_DIR}/tuyaos_adapter/tuyaopen/interface")

# [tuyaopen-core integration] ops 卡（tal_api_ops_platform / tal_network_ops_platform /
# tal_netif_ops_platform）由**平台自己的构建**编入最终 ELF：注册集中在
# tuyaos_adapter/tuyaopen/tuyaopen-core_ops.c（薄卡），随 adapter 的 SOURCES 一起编；
# 平台实现文件（src/system/tkl_*.c）保持上游原样零改动。
# 根 CMakeLists 不涉及平台路径、也不再有 port 目标。
# 注：lfs 头路径不再由平台补——littlefs 随 src/tuya_service/lib_kv 组件编入，
# include 经 COMPONENT_PUBINC 传播（audio_player 的 datasink_file 同用）。
