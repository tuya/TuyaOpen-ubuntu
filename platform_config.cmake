list_subdirectories(PLATFORM_PUBINC ${PLATFORM_PATH}/tuyaos_adapter)

# [tuyaopen-core integration] 契约层（C2，与 dev 树 linux/esp32 一致）：core 与 port
# 共用的 ops 类型（tal_api_ops.h / tal_kv_ops.h / tal_netif_ops.h /
# tal_network_ops.h / tal_bluetooth_ops.h + tal_bluetooth_def.h）物理放在本平台
# port/interface/；core 的 tal_*.h 反向 include 它们（经 PLATFORM_PUBINC →
# tuyaopen-headers 注入 core 编译单元），port 侧由 adapter cmake 注入。
list(APPEND PLATFORM_PUBINC "${CMAKE_CURRENT_LIST_DIR}/port/interface")

# [tuyaopen-core integration] 本平台的 port（ops 卡实现，见 ./port/）由**平台自己的
# 构建**编入最终 ELF（见 tuyaos_adapter/tuyaopen_adapter.cmake 的 SOURCES 追加），
# 根 CMakeLists 不涉及平台路径、也不再有 port 目标。
# 这里只补一条上游组件的头路径：lfs（audio_player 的 datasink_file 使用；实现由 port 的 littlefs 提供）
list(APPEND HEADER_DIR "${CMAKE_CURRENT_LIST_DIR}/port/littlefs")
