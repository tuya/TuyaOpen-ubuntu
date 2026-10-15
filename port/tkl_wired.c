/**
 * @file tkl_wired.c
 * @brief TAL_NETIF_OPS_T 卡：复用 platform 的 tkl_wired 实现。
 *
 * get_ip 签名逐字一致直接绑定；set_link_cb 需薄包装——core 的
 * TAL_NETIF_LINK_CHANGE_CB(void(*)(TAL_NETIF_LINK_STAT_E)) 与 tkl 的
 * TKL_WIRED_STATUS_CHANGE_CB(void(*)(TKL_WIRED_STAT_E)) 是两个不同 C 类型
 * （值域一致 DOWN=0/UP=1），故存下 core 回调、向 tkl 注册转投 shim。
 *
 * tkl_wired.c 在 platform 侧由 CONFIG_ENABLE_WIRED 门控编译。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_netif_ops.h"

#include "tkl_wired.h"

static TAL_NETIF_LINK_CHANGE_CB s_link_cb;

static void __link_cb_shim(TKL_WIRED_STAT_E status)
{
    if (s_link_cb) {
        s_link_cb((TAL_NETIF_LINK_STAT_E)status);
    }
}

static OPERATE_RET __set_link_cb(TAL_NETIF_LINK_CHANGE_CB cb)
{
    s_link_cb = cb;
    return tkl_wired_set_status_cb(__link_cb_shim);
}

TAL_NETIF_OPS_T tal_netif_ops_platform = {
    .get_ip      = tkl_wired_get_ip,
    .set_link_cb = __set_link_cb,
};
