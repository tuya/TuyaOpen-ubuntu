/**
 * @file tkl_asr.h
 * @brief Stub implementation for ASR (Automatic Speech Recognition) on Ubuntu
 *
 * This is a stub/dummy implementation for Ubuntu platform.
 * ASR functionality is not available on Ubuntu - use keyboard input instead.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __TKL_ASR_H__
#define __TKL_ASR_H__

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
    TKL_ASR_WAKEUP_WORD_UNKNOWN = 0,
    TKL_ASR_WAKEUP_NIHAO_TUYA,
    TKL_ASR_WAKEUP_NIHAO_XIAOZHI,
    TKL_ASR_WAKEUP_XIAOZHI_TONGXUE,
    TKL_ASR_WAKEUP_XIAOZHI_GUANJIA,
} TKL_ASR_WAKEUP_WORD_E;

/***********************************************************
********************function declaration********************
***********************************************************/

/**
 * @brief Initialize ASR (stub - not implemented on Ubuntu)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_asr_init(void)
{
    // Stub: ASR not available on Ubuntu
    return OPRT_OK;
}

/**
 * @brief Deinitialize ASR (stub)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_asr_deinit(void)
{
    return OPRT_OK;
}

/**
 * @brief Configure ASR wakeup words (stub)
 * @return OPERATE_RET
 */
static inline OPERATE_RET tkl_asr_wakeup_word_config(TKL_ASR_WAKEUP_WORD_E *words, uint32_t count)
{
    return OPRT_OK;
}

/**
 * @brief Get ASR process unit size (stub)
 * @return uint32_t
 */
static inline uint32_t tkl_asr_get_process_uint_size(void)
{
    return 320; // Typical 10ms frame at 16kHz
}

/**
 * @brief Recognize wakeup word (stub - always returns unknown)
 * @return TKL_ASR_WAKEUP_WORD_E
 */
static inline TKL_ASR_WAKEUP_WORD_E tkl_asr_recognize_wakeup_word(uint8_t *data, uint32_t len)
{
    return TKL_ASR_WAKEUP_WORD_UNKNOWN;
}

#ifdef __cplusplus
}
#endif

#endif /* __TKL_ASR_H__ */

