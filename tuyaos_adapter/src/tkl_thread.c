/**
 * @file tkl_thread.c
 * @brief LINUX TKL thread — pthread behind TAL (aligned for pj TLS identity)
 * @version 0.2
 * @date 2026-08-06
 *
 * @copyright Copyright 2020-2021 Tuya Inc. All Rights Reserved.
 *
 * @note tkl_thread_get_id() returns pthread_self() as the task identity so
 *       os_core_tuyaos TLS rows match across TAL-created and foreign threads.
 *       tkl_thread_is_self() accepts a create-handle (THREAD_DATA*).
 */

#include "tuya_iot_config.h"
#include "tkl_thread.h"
#include "tkl_memory.h"
#include <pthread.h>
#include <sys/prctl.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    pthread_t     id;
    THREAD_FUNC_T func;
    void         *arg;
} THREAD_DATA;

/**
 * @brief pthread entry wrapper
 * @param[in] arg THREAD_DATA*
 * @return NULL
 */
static void *_tkl_thread_wrap_func(void *arg)
{
    THREAD_DATA *thread_data = (THREAD_DATA *)arg;
    if (thread_data && thread_data->func) {
        thread_data->func(thread_data->arg);
    }
    return NULL;
}

/**
 * @brief Create thread
 * @param[out] thread thread handle (THREAD_DATA*)
 * @param[in] name thread name
 * @param[in] stack_size stack size in bytes (0 = OS default)
 * @param[in] priority unused on LINUX
 * @param[in] func thread entry
 * @param[in] arg entry arg
 * @return OPRT_OK on success
 * @note Detached: join is done via TAL/pj join semaphore, not pthread_join.
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_create(TKL_THREAD_HANDLE *thread, const char *name, uint32_t stack_size,
                                                   uint32_t priority, THREAD_FUNC_T func, void *const arg)
{
    int ret;
    THREAD_DATA *thread_data;
    pthread_attr_t attr;

    (void)priority;
    (void)name;
    if (!thread) {
        return OPRT_INVALID_PARM;
    }

    thread_data = (THREAD_DATA *)tkl_system_malloc(sizeof(THREAD_DATA));
    if (thread_data == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(thread_data, 0, sizeof(THREAD_DATA));
    thread_data->func = func;
    thread_data->arg = arg;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (stack_size > 0) {
        /* pthread may require a minimum; ignore failure and keep default */
        (void)pthread_attr_setstacksize(&attr, (size_t)stack_size);
    }
    ret = pthread_create(&(thread_data->id), &attr, _tkl_thread_wrap_func, thread_data);
    pthread_attr_destroy(&attr);
    if (0 != ret) {
        tkl_system_free(thread_data);
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }

    *thread = (TKL_THREAD_HANDLE)thread_data;
    return OPRT_OK;
}

/**
 * @brief Release thread handle storage (thread already detached)
 * @param[in] thread thread handle
 * @return OPRT_OK on success
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_release(TKL_THREAD_HANDLE thread)
{
    THREAD_DATA *thread_data;

    if (!thread) {
        return OPRT_INVALID_PARM;
    }
    thread_data = (THREAD_DATA *)thread;
    tkl_system_free(thread_data);
    return OPRT_OK;
}

/**
 * @brief Get stack watermark (not supported on LINUX)
 * @param[in] thread thread handle
 * @param[out] watermark always -1
 * @return OPRT_OK
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_get_watermark(TKL_THREAD_HANDLE thread, uint32_t *watermark)
{
    (void)thread;
    if (watermark) {
        *watermark = (uint32_t)-1;
    }
    return OPRT_OK;
}

/**
 * @brief Get calling task identity for TLS / affinity
 * @param[out] thread identity (pthread_t cast to pointer)
 * @return OPRT_OK on success
 * @note Not the same object as create-handle; compare identities with == only.
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_get_id(TKL_THREAD_HANDLE *thread)
{
    if (!thread) {
        return OPRT_INVALID_PARM;
    }
    *thread = (TKL_THREAD_HANDLE)(uintptr_t)pthread_self();
    return OPRT_OK;
}

/**
 * @brief Set name of self thread
 * @param[in] name thread name
 * @return OPRT_OK on success
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_set_self_name(const char *name)
{
    if (!name) {
        return OPRT_INVALID_PARM;
    }
    prctl(PR_SET_NAME, name);
    return OPRT_OK;
}

/**
 * @brief Check if create-handle refers to the calling thread
 * @param[in] thread create-handle from tkl_thread_create
 * @param[out] is_self TRUE if same pthread
 * @return OPRT_OK on success
 */
TUYA_WEAK_ATTRIBUTE OPERATE_RET tkl_thread_is_self(TKL_THREAD_HANDLE thread, BOOL_T *is_self)
{
    THREAD_DATA *thread_data;

    if (NULL == thread || NULL == is_self) {
        return OPRT_INVALID_PARM;
    }
    thread_data = (THREAD_DATA *)thread;
    *is_self = pthread_equal(thread_data->id, pthread_self()) ? TRUE : FALSE;
    return OPRT_OK;
}

/**
 * @brief Diagnose thread (not supported)
 * @param[in] thread thread handle
 * @return OPRT_NOT_SUPPORTED
 */
OPERATE_RET tkl_thread_diagnose(TKL_THREAD_HANDLE thread)
{
    (void)thread;
    return OPRT_NOT_SUPPORTED;
}
