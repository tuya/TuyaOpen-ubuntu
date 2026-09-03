/**
 * @file tkl_venc_mpp.h
 * @brief Hardware video encoder on Rockchip SoCs, via the MPP service.
 *
 * RK3576 exposes no V4L2 memory-to-memory encoder: a full scan of every
 * /dev/video* node reports capture or output roles only. Hardware encoding goes
 * through /dev/mpp_service instead, which librockchip_mpp wraps. This file is
 * the TKL-side wrapper so the board driver never touches the vendor API.
 *
 * Input is NV12 (MPP_FMT_YUV420SP), which is what the rkisp/rkvpss capture
 * nodes already produce, so no colour conversion sits in the path.
 */

#ifndef __TKL_VENC_MPP_H__
#define __TKL_VENC_MPP_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The receiving player decides what it can decode, so the caller picks. */
typedef enum {
    TKL_VENC_CODEC_H264 = 0,
    TKL_VENC_CODEC_H265,
} TKL_VENC_CODEC_E;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;          /* 0 -> 30 */
    uint32_t bitrate_kbps; /* 0 -> derived from resolution */
    uint32_t gop;          /* key frame interval in frames, 0 -> fps * 2 */
    TKL_VENC_CODEC_E codec;
} TKL_VENC_MPP_CFG_T;

typedef void *TKL_VENC_MPP_HANDLE_T;

/**
 * @brief Create an encoder instance.
 * @param[out] hdl encoder handle
 * @param[in]  cfg geometry, rate control and codec
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_venc_mpp_open(TKL_VENC_MPP_HANDLE_T *hdl, const TKL_VENC_MPP_CFG_T *cfg);

/**
 * @brief Encode one NV12 frame.
 *
 * @param[in]  hdl          encoder handle
 * @param[in]  nv12         frame data, height rows of Y then height/2 rows of UV
 * @param[in]  len          bytes in @p nv12, must cover width*height*3/2
 * @param[out] out          encoded Annex-B data, owned by the encoder and valid
 *                          until the next call on this handle
 * @param[out] out_len      encoded byte count
 * @param[out] is_keyframe  TRUE when the encoder emitted an IDR
 * @return OPRT_OK on success. OPRT_RESOURCE_NOT_READY when the encoder consumed
 *         the frame but has no packet yet, which is not an error.
 */
OPERATE_RET tkl_venc_mpp_encode(TKL_VENC_MPP_HANDLE_T hdl, const uint8_t *nv12, uint32_t len, uint8_t **out,
                                uint32_t *out_len, BOOL_T *is_keyframe);

/**
 * @brief Force the next encoded frame to be an IDR.
 *
 * Used when a viewer joins mid-stream, or after the sender dropped frames and
 * needs a clean restart point.
 */
OPERATE_RET tkl_venc_mpp_request_idr(TKL_VENC_MPP_HANDLE_T hdl);

/**
 * @brief Correct the frame rate rate control is working from.
 *
 * Rate control derives its per-frame bit budget as bitrate/fps, so being told
 * a rate the source does not actually deliver makes the encoder overshoot the
 * configured bitrate by exactly requested/actual. Capture nodes that fix their
 * rate in hardware often refuse to report it at all (VIDIOC_G_PARM returns
 * ENOTTY), leaving measurement as the only way to find out - hence this being
 * a runtime correction rather than an open() parameter.
 *
 * @param[in] hdl encoder handle
 * @param[in] fps measured frames per second
 * @return OPRT_OK on success, OPRT_INVALID_PARM on a bad handle or zero fps
 */
OPERATE_RET tkl_venc_mpp_set_fps(TKL_VENC_MPP_HANDLE_T hdl, uint32_t fps);

/**
 * @brief Change the bitrate rate control is aiming for.
 *
 * Live streaming needs this because the link, not the product spec, decides
 * how much video can actually be delivered: when the transport cannot drain
 * what the encoder produces, lowering the target degrades the picture smoothly
 * where the alternative is discarding whole frames.
 *
 * @param[in] hdl  encoder handle
 * @param[in] kbps new target bitrate
 * @return OPRT_OK on success, OPRT_INVALID_PARM on a bad handle or zero rate
 */
OPERATE_RET tkl_venc_mpp_set_bitrate(TKL_VENC_MPP_HANDLE_T hdl, uint32_t kbps);

OPERATE_RET tkl_venc_mpp_close(TKL_VENC_MPP_HANDLE_T hdl);

#ifdef __cplusplus
}
#endif

#endif /* __TKL_VENC_MPP_H__ */
