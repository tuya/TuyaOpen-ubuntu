/**
 * @file tkl_network.c
 * @brief 标准网络卡：绑定 platform（TuyaOpen-ubuntu）的 tkl_net_* 实现。
 *
 * core 的 tal_network.c 经 extern 引用 tal_network_ops_platform 并完成
 * tal_net_* 分发；实现位于 platform/LINUX/tuyaos_adapter/src/tkl_network.c
 * （声明见 include/network/tkl_network.h）。移植新平台时在各自 port 的
 * tkl_network.c 中提供同名卡即可。
 * 卡成员与实现同开关：可选能力（局域网/对端服务、p2p）的 ops 仅在对应宏
 * 定义时才挂载，关掉时成员为 NULL，core 侧 TAL_NET_EXEC_OP 会打印
 * "not available"。
 *
 * 零 core 依赖（分层约束：port 只向下依赖平台层）：卡类型取契约层
 * interface/tal_network_ops.h，不 include core 头、不调 core 函数。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_network_ops.h"

#include "tkl_network.h"

TAL_NETWORK_OPS_T tal_network_ops_platform = {
    .name = "tkl",
    .type = TAL_NET_TYPE_PLATFORM,
    .ipaddr = 0,
    .ops =
        {
            .get_errno = tkl_net_get_errno,
            .fd_set = tkl_net_fd_set,
            .fd_isset = tkl_net_fd_isset,
            .fd_zero = tkl_net_fd_zero,
            .select = tkl_net_select,
            .get_nonblock = tkl_net_get_nonblock,
            .set_block = tkl_net_set_block,
            .close = tkl_net_close,
            .socket_create = tkl_net_socket_create,
            .connect = tkl_net_connect,
            .bind = tkl_net_bind,
            .send = tkl_net_send,
            .recv = tkl_net_recv,
            .set_timeout = tkl_net_set_timeout,
            .set_reuse = tkl_net_set_reuse,
            .disable_nagle = tkl_net_disable_nagle,
            .gethostbyname = tkl_net_gethostbyname,
            .set_keepalive = tkl_net_set_keepalive,
            .str2addr = tkl_net_str2addr,
            .addr2str = tkl_net_addr2str,
            .setsockopt = tkl_net_setsockopt,
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P) || defined(ENABLE_AI_COMPONENTS)
            /* 局域网/对端服务成员 */
            .listen = tkl_net_listen,
            .accept = tkl_net_accept,
#endif
#if defined(ENABLE_LOCAL_LINK) || defined(ENABLE_TUYA_P2P)
            .send_to = tkl_net_send_to,
            .recvfrom = tkl_net_recvfrom,
#endif
#if defined(ENABLE_LOCAL_LINK)
            /* 局域网协议专用成员（tuya_lan.c 收帧） */
            .recv_nd_size = tkl_net_recv_nd_size,
#endif
#if defined(ENABLE_TUYA_P2P)
            /* p2p 专用成员 */
            .fd_clear = tkl_net_fd_clear,
            .shutdown = tkl_net_shutdown,
            .set_bufsize = tkl_net_set_bufsize,
            .getsockname = tkl_net_getsockname,
            .getpeername = tkl_net_getpeername,
#endif
        },
};
