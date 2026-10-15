/**
 * @file tkl_system.c
 * @brief TAL_API_OPS_T 卡：直接绑定 platform（TuyaOpen-ubuntu）的 tkl 实现。
 *
 * core 的 tal_api_ops.h 契约：句柄/回调类型与 TKL 层同为 void* 别名/同函数指针
 * 形态，签名逐条核对一致（见 tuyaopen-core-integration-plan.md §3.4 映射表），
 * 无需包装。tkl_* 符号由平台二次构建（build_example.py）编入最终 ELF。
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "tal_api_ops.h"

#include "tkl_memory.h"
#include "tkl_system.h"
#include "tkl_semaphore.h"
#include "tkl_mutex.h"
#include "tkl_thread.h"
#include "tkl_queue.h"
#include "tkl_output.h"

TAL_API_OPS_T tal_api_ops_platform = {
    //! log output
    .log_output = tkl_log_output,
    //! memory
    .malloc = tkl_system_malloc,
    .free   = tkl_system_free,
    //! system
    .sleep           = tkl_system_sleep,
    .reset           = tkl_system_reset,
    .get_millisecond = tkl_system_get_millisecond,
    .get_random      = tkl_system_get_random,
    //! semaphore
    .semaphore_create_init = tkl_semaphore_create_init,
    .semaphore_wait        = tkl_semaphore_wait,
    .semaphore_post        = tkl_semaphore_post,
    .semaphore_release     = tkl_semaphore_release,
    //! mutex
    .mutex_create_init = tkl_mutex_create_init,
    .mutex_lock        = tkl_mutex_lock,
    .mutex_unlock      = tkl_mutex_unlock,
    .mutex_release     = tkl_mutex_release,
    //! thread
    .thread_create        = tkl_thread_create,
    .thread_release       = tkl_thread_release,
    .thread_set_self_name = tkl_thread_set_self_name,
    .thread_is_self       = tkl_thread_is_self,
    .thread_get_id        = tkl_thread_get_id,
    //! queue
    .queue_create_init = tkl_queue_create_init,
    .queue_post        = tkl_queue_post,
    .queue_fetch       = tkl_queue_fetch,
    .queue_free        = tkl_queue_free,
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    //! ex ram（外部 RAM，如 PSRAM）：复用平台既有 tkl_system_psram_*（Linux 无独立外部
    //! RAM，平台实现退化为普通内存）；宏隔离与 core 的 TAL_API_OPS_T 一致
    .ex_ram_malloc = tkl_system_psram_malloc,
    .ex_ram_free   = tkl_system_psram_free,
#endif
};
