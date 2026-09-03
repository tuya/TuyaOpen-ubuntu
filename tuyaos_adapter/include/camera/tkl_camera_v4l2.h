/**
 * @file tkl_camera_v4l2.h
 * @brief Linux V4L2 camera TKL interface.
 *
 * Covers both device classes seen on the supported boards:
 * - USB/UVC cameras, which are single-planar (V4L2_CAP_VIDEO_CAPTURE) and
 *   usually expose YUYV and MJPEG.
 * - SoC CSI/ISP pipelines such as Rockchip rkisp/rkvpss, which are multiplanar
 *   (V4L2_CAP_VIDEO_CAPTURE_MPLANE) and expose UYVY/NV12/NV16 but no YUYV.
 *
 * The single/multi planar API is picked automatically from VIDIOC_QUERYCAP, so
 * callers only choose a pixel format.
 */

#ifndef __TKL_CAMERA_V4L2_H__
#define __TKL_CAMERA_V4L2_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TKL_CAMERA_V4L2_PIXFMT_YUYV = 0, /* YUV422 packed, Y0 U Y1 V — typical UVC  */
    TKL_CAMERA_V4L2_PIXFMT_MJPEG,    /* Motion JPEG                            */
    TKL_CAMERA_V4L2_PIXFMT_UYVY,     /* YUV422 packed, U Y0 V Y1 — rkisp/rkvpss */
    TKL_CAMERA_V4L2_PIXFMT_NV12,     /* YUV420 semi-planar, encoder input       */
    TKL_CAMERA_V4L2_PIXFMT_NV16,     /* YUV422 semi-planar                      */
} TKL_CAMERA_V4L2_PIXFMT_E;

typedef struct {
    const char *devnode;   /* e.g. "/dev/video0" */
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t buffer_count; /* request buffer count, typical 4 */
    TKL_CAMERA_V4L2_PIXFMT_E pixfmt;
} TKL_CAMERA_V4L2_CFG_T;

typedef void *TKL_CAMERA_V4L2_HANDLE_T;

/** Bit N of the probe mask corresponds to TKL_CAMERA_V4L2_PIXFMT_E value N. */
#define TKL_CAMERA_V4L2_PIXFMT_BIT(fmt) (1u << (uint32_t)(fmt))

/**
 * @brief Probe which pixel formats a capture node offers, without streaming.
 *
 * Opens the node, walks VIDIOC_ENUM_FMT on whichever buffer type the device
 * reports, and closes it again. Lets a driver pick a format the hardware
 * actually has instead of assuming one and failing at open.
 *
 * @param[in]  devnode      V4L2 device node path, e.g. "/dev/video0"
 * @param[out] pixfmt_mask  bitmask built with TKL_CAMERA_V4L2_PIXFMT_BIT()
 * @return OPRT_OK on success. The mask may be 0 if nothing is recognised.
 */
OPERATE_RET tkl_camera_v4l2_probe(const char *devnode, uint32_t *pixfmt_mask);

OPERATE_RET tkl_camera_v4l2_open(TKL_CAMERA_V4L2_HANDLE_T *hdl, const TKL_CAMERA_V4L2_CFG_T *cfg);

/**
 * @brief Frame rate the node is actually running at, after open().
 *
 * The requested rate in TKL_CAMERA_V4L2_CFG_T is only a request: SoC ISP
 * pipelines commonly fix the rate in the sensor clock tree and reject
 * VIDIOC_S_PARM entirely. Anything that divides by the frame rate - encoder
 * rate control above all - must use this value, not the requested one.
 *
 * @param[in]  hdl camera handle from tkl_camera_v4l2_open()
 * @param[out] fps actual frames per second
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_camera_v4l2_get_fps(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t *fps);
OPERATE_RET tkl_camera_v4l2_start(TKL_CAMERA_V4L2_HANDLE_T hdl);
OPERATE_RET tkl_camera_v4l2_stop(TKL_CAMERA_V4L2_HANDLE_T hdl);
OPERATE_RET tkl_camera_v4l2_close(TKL_CAMERA_V4L2_HANDLE_T hdl);

/**
 * @brief Dequeue one captured buffer.
 *
 * @param hdl     camera handle
 * @param data    output pointer to buffer data (mmap'ed)
 * @param len     output bytes used
 * @param index   output buffer index (must be passed to queue)
 */
OPERATE_RET tkl_camera_v4l2_dequeue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint8_t **data, uint32_t *len, uint32_t *index);
OPERATE_RET tkl_camera_v4l2_queue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* __TKL_CAMERA_V4L2_H__ */
