/**
 * @file tkl_vad.h
 * @brief Stub implementation for VAD (Voice Activity Detection) on Ubuntu
 *
 * This is a stub/dummy implementation for Ubuntu platform.
 * VAD functionality is simulated using simple threshold detection.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __TKL_VAD_H__
#define __TKL_VAD_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    TKL_VAD_STATUS_NONE = 0,
    TKL_VAD_STATUS_SPEECH,
    TKL_VAD_STATUS_NOISE,
} TKL_VAD_STATUS_E;

typedef struct {
    uint32_t sample_rate;
    uint32_t channel_num;
    uint32_t speech_min_ms;
    uint32_t noise_min_ms;
    float scale;
    uint32_t frame_duration_ms;
} TKL_VAD_CONFIG_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
static TKL_VAD_STATUS_E g_vad_status = TKL_VAD_STATUS_NONE;
static bool g_vad_started = false;

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Initialize VAD (stub - simplified implementation)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_vad_init(TKL_VAD_CONFIG_T *config)
{
    // Stub: Simple VAD initialization
    g_vad_status = TKL_VAD_STATUS_NONE;
    return OPRT_OK;
}

/**
 * @brief Deinitialize VAD (stub)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_vad_deinit(void)
{
    g_vad_status = TKL_VAD_STATUS_NONE;
    g_vad_started = false;
    return OPRT_OK;
}

/**
 * @brief Start VAD detection (stub)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_vad_start(void)
{
    g_vad_started = true;
    g_vad_status = TKL_VAD_STATUS_SPEECH; // Always assume speech when started
    return OPRT_OK;
}

/**
 * @brief Stop VAD detection (stub)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_vad_stop(void)
{
    g_vad_started = false;
    g_vad_status = TKL_VAD_STATUS_NONE;
    return OPRT_OK;
}

/**
 * @brief Feed audio data to VAD (stub - simplified)
 * @return void
 */
static inline void tkl_vad_feed(uint8_t *data, uint32_t len)
{
    // Stub: Simple energy-based VAD
    // In a real implementation, this would analyze the audio
    // For now, just assume speech if VAD is started
    if (g_vad_started) {
        g_vad_status = TKL_VAD_STATUS_SPEECH;
    }
}

/**
 * @brief Get current VAD status (stub)
 * @return TKL_VAD_STATUS_E
 */
static inline TKL_VAD_STATUS_E tkl_vad_get_status(void)
{
    return g_vad_status;
}

#ifdef __cplusplus
}
#endif

#endif /* __TKL_VAD_H__ */

