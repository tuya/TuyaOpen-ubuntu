/**
 * @file netif_ops.c
 * @brief CORE_NETIF_OPS_T implementation for Linux.
 *
 * Follows the upstream TAL behaviour: the local IPv4 address comes from the
 * Wi-Fi station first (tkl_wifi_get_ip(WF_STATION)) and from the wired link
 * otherwise, each path gated by ENABLE_WIFI / ENABLE_WIRED. Link state is
 * polled (the TKL wired status callback is a single slot and is owned by
 * netmgr), reporting up when the Wi-Fi station has an IP or any wired
 * non-loopback interface is up. Interface enumeration walks the POSIX
 * interface list, as the upstream P2P port does.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core_netif_ops.h"
#include "tuya_iot_config.h"
#include "tkl_system.h"
#include "tkl_thread.h"

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "tkl_wifi.h"
#endif
#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
#include "tkl_wired.h"
#endif

/* Link state polling period. */
#define NETIF_LINK_POLL_MS 3000

static CORE_NETIF_LINK_CHANGE_CB s_link_cb;
static TKL_THREAD_HANDLE         s_link_thread;
static int                       s_last_link = -1;

static int __is_loopback(const char *name)
{
    if (NULL == name) {
        return 0;
    }
    return (0 == strcmp(name, "lo") || 0 == strncmp(name, "loopback", 8));
}

static CORE_NETIF_LINK_STAT_E __netif_link_get(void)
{
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    {
        WF_STATION_STAT_E wifi_stat;

        if ((OPRT_OK == tkl_wifi_station_get_status(&wifi_stat)) &&
            ((WSS_GOT_IP == wifi_stat) || (WSS_CONN_SUCCESS == wifi_stat))) {
            return CORE_NETIF_LINK_UP;
        }
    }
#endif

#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    {
        TKL_WIRED_STAT_E wired_stat;

        if ((OPRT_OK == tkl_wired_get_status(&wired_stat)) && (TKL_WIRED_LINK_UP == wired_stat)) {
            return CORE_NETIF_LINK_UP;
        }
    }
#endif

    return CORE_NETIF_LINK_DOWN;
}

static void __netif_link_poll(void *arg)
{
    (void)arg;

    for (;;) {
        CORE_NETIF_LINK_STAT_E link = __netif_link_get();

        if ((NULL != s_link_cb) && ((int)link != s_last_link)) {
            s_last_link = (int)link;
            s_link_cb(link);
        }
        tkl_system_sleep(NETIF_LINK_POLL_MS);
    }
}

static OPERATE_RET __netif_get_ip(NW_IP_S *ip)
{
    if (NULL == ip) {
        return OPRT_INVALID_PARM;
    }

    memset(ip, 0, sizeof(*ip));

#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    if ((OPRT_OK == tkl_wifi_get_ip(WF_STATION, ip)) && ('\0' != ip->ip[0])) {
        return OPRT_OK;
    }
    memset(ip, 0, sizeof(*ip));
#endif

#if defined(ENABLE_WIRED) && (ENABLE_WIRED == 1)
    if ((OPRT_OK == tkl_wired_get_ip(ip)) && ('\0' != ip->ip[0])) {
        return OPRT_OK;
    }
#endif

    return OPRT_NOT_FOUND;
}

static OPERATE_RET __netif_set_link_cb(CORE_NETIF_LINK_CHANGE_CB cb)
{
    s_link_cb = cb;

    if (NULL == cb) {
        return OPRT_OK;
    }
    if (NULL != s_link_thread) {
        return OPRT_OK;
    }

    /* Report the current state right after registration. */
    s_last_link = (int)__netif_link_get();
    cb(s_last_link);

    return tkl_thread_create(&s_link_thread, "netif_link", 4096, 2, __netif_link_poll, NULL);
}

static int __netif_iface_addr(const char *name, CORE_NETIF_ADDR_T *addr)
{
    int          sock;
    struct ifreq ifr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name) - 1);

    if (ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
        close(sock);
        return -1;
    }
    memset(addr, 0, sizeof(*addr));
    strncpy(addr->name, name, sizeof(addr->name) - 1);
    strncpy(addr->ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), sizeof(addr->ip) - 1);

    if (0 == ioctl(sock, SIOCGIFNETMASK, &ifr)) {
        strncpy(addr->mask, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr),
                sizeof(addr->mask) - 1);
    }

    close(sock);
    return 0;
}

/* Enumerate up, non-loopback interfaces carrying an IPv4 address; returning
 * true from the callback stops the walk. */
static OPERATE_RET __netif_foreach(CORE_NETIF_ADDR_CB cb, void *user_data)
{
    struct if_nameindex *list;
    int                  i;
    int                  found = 0;

    if (NULL == cb) {
        return OPRT_INVALID_PARM;
    }

    list = if_nameindex();
    if (NULL == list) {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; 0 != list[i].if_index; i++) {
        CORE_NETIF_ADDR_T addr;
        struct ifreq      ifr;
        int               sock;

        if (__is_loopback(list[i].if_name)) {
            continue;
        }

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            continue;
        }
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, list[i].if_name, sizeof(ifr.ifr_name) - 1);
        if (0 != ioctl(sock, SIOCGIFFLAGS, &ifr)) {
            close(sock);
            continue;
        }
        close(sock);
        if (0 == (ifr.ifr_flags & IFF_UP)) {
            continue;
        }

        if (0 != __netif_iface_addr(list[i].if_name, &addr)) {
            continue;
        }

        found = 1;
        if (cb(&addr, user_data)) {
            break;
        }
    }

    if_freenameindex(list);
    return found ? OPRT_OK : OPRT_NOT_FOUND;
}

CORE_NETIF_OPS_T core_netif_ops_platform = {
    .get_ip       = __netif_get_ip,
    .set_link_cb  = __netif_set_link_cb,
    .foreach_addr = __netif_foreach,
};
