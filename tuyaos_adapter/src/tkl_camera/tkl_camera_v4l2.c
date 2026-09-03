#include "camera/tkl_camera_v4l2.h"
#include "tal_log.h"
#include "tuya_error_code.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* Frames are handed up as one contiguous buffer, so only single-memory-plane
 * formats can be represented. NV12/NV16/UYVY report num_planes == 1 even on the
 * multiplanar API; the genuinely non-contiguous variants (NM12/NM21) do not and
 * are rejected at open time instead of silently delivering only the Y plane. */
#define TKL_V4L2_MAX_PLANES 1

typedef struct {
    void *start;
    size_t length;
} v4l2_buf_t;

typedef struct {
    int fd;
    bool streaming;

    /* VIDIOC_QUERYCAP decides this: UVC cameras are single-planar, SoC ISP
     * pipelines (rkisp/rkvpss) are multiplanar. Every buffer ioctl branches. */
    bool mplane;
    enum v4l2_buf_type buf_type;

    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t buffer_count;
    uint32_t fourcc;

    v4l2_buf_t *buffers;
    uint32_t buffers_num;
} tkl_v4l2_ctx_t;

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

/**
 * @brief Fill a v4l2_buffer for the active API, attaching a plane array when
 *        the device is multiplanar.
 * @param ctx    camera context
 * @param buf    buffer to initialise (zeroed by this function)
 * @param planes caller-owned plane array, must outlive the ioctl
 * @param index  buffer index
 */
static void v4l2_buf_prepare(const tkl_v4l2_ctx_t *ctx, struct v4l2_buffer *buf, struct v4l2_plane *planes,
                             uint32_t index)
{
    memset(buf, 0, sizeof(*buf));
    buf->type = ctx->buf_type;
    buf->memory = V4L2_MEMORY_MMAP;
    buf->index = index;

    if (ctx->mplane) {
        memset(planes, 0, sizeof(*planes) * TKL_V4L2_MAX_PLANES);
        buf->m.planes = planes;
        buf->length = TKL_V4L2_MAX_PLANES;
    }
}

static OPERATE_RET v4l2_set_format_and_fps(tkl_v4l2_ctx_t *ctx)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = ctx->buf_type;

    if (ctx->mplane) {
        fmt.fmt.pix_mp.width = ctx->width;
        fmt.fmt.pix_mp.height = ctx->height;
        fmt.fmt.pix_mp.pixelformat = ctx->fourcc;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
        fmt.fmt.pix_mp.num_planes = 1;
    } else {
        fmt.fmt.pix.width = ctx->width;
        fmt.fmt.pix.height = ctx->height;
        fmt.fmt.pix.pixelformat = ctx->fourcc;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
    }

    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == -1) {
        return OPRT_COM_ERROR;
    }

    /* The driver may negotiate a different geometry or refuse the format; take
     * back what it actually set and make sure we can still represent it. */
    if (ctx->mplane) {
        if (fmt.fmt.pix_mp.pixelformat != ctx->fourcc) {
            return OPRT_NOT_SUPPORTED;
        }
        if (fmt.fmt.pix_mp.num_planes != 1) {
            /* e.g. NM12/NM21: separate Y and UV allocations, cannot be passed
             * up through the single-pointer frame API. */
            return OPRT_NOT_SUPPORTED;
        }
        ctx->width = fmt.fmt.pix_mp.width;
        ctx->height = fmt.fmt.pix_mp.height;
    } else {
        if (fmt.fmt.pix.pixelformat != ctx->fourcc) {
            return OPRT_NOT_SUPPORTED;
        }
        ctx->width = fmt.fmt.pix.width;
        ctx->height = fmt.fmt.pix.height;
    }

    /*
     * Frame rate. Same contract as the format above: ask, then take back what
     * the driver actually did. Plenty of capture nodes - SoC ISP pipelines in
     * particular - reject VIDIOC_S_PARM outright with ENOTTY because the rate
     * is fixed by the sensor and its clock tree; others silently clamp it.
     * ctx->fps must end up holding the real rate, because everything
     * downstream divides by it: an encoder told 25 while being fed 30 frames a
     * second computes its per-frame bit budget from the wrong denominator and
     * overshoots the configured bitrate by exactly that ratio.
     */
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = ctx->buf_type;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = (ctx->fps == 0) ? 30 : ctx->fps;
    if (xioctl(ctx->fd, VIDIOC_S_PARM, &parm) == -1) {
        /* Not settable. Read the rate the hardware runs at instead. */
        memset(&parm, 0, sizeof(parm));
        parm.type = ctx->buf_type;
        if (xioctl(ctx->fd, VIDIOC_G_PARM, &parm) == -1) {
            PR_WARN("v4l2 fps neither settable nor readable, keeping requested %u", ctx->fps);
            return OPRT_OK;
        }
    }

    {
        uint32_t num = parm.parm.capture.timeperframe.numerator;
        uint32_t den = parm.parm.capture.timeperframe.denominator;

        if (num > 0 && den > 0) {
            uint32_t actual = den / num;

            if (actual > 0 && actual != ctx->fps) {
                PR_WARN("v4l2 fps %u not honoured, hardware runs at %u; using that", ctx->fps, actual);
                ctx->fps = actual;
            }
        }
    }

    return OPRT_OK;
}

/**
 * @brief Map a V4L2 fourcc back to the TKL pixel format enum.
 * @return the enum value, or -1 when the fourcc is not one we handle
 */
static int v4l2_fourcc_to_pixfmt(uint32_t fourcc)
{
    switch (fourcc) {
    case V4L2_PIX_FMT_YUYV:
        return TKL_CAMERA_V4L2_PIXFMT_YUYV;
    case V4L2_PIX_FMT_MJPEG:
        return TKL_CAMERA_V4L2_PIXFMT_MJPEG;
    case V4L2_PIX_FMT_UYVY:
        return TKL_CAMERA_V4L2_PIXFMT_UYVY;
    case V4L2_PIX_FMT_NV12:
        return TKL_CAMERA_V4L2_PIXFMT_NV12;
    case V4L2_PIX_FMT_NV16:
        return TKL_CAMERA_V4L2_PIXFMT_NV16;
    default:
        return -1;
    }
}

OPERATE_RET tkl_camera_v4l2_probe(const char *devnode, uint32_t *pixfmt_mask)
{
    if (devnode == NULL || pixfmt_mask == NULL) {
        return OPRT_INVALID_PARM;
    }
    *pixfmt_mask = 0;

    int fd = open(devnode, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return OPRT_COM_ERROR;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        close(fd);
        return OPRT_COM_ERROR;
    }

    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    enum v4l2_buf_type type;
    if (caps & V4L2_CAP_VIDEO_CAPTURE) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        close(fd);
        return OPRT_NOT_SUPPORTED;
    }

    for (uint32_t i = 0;; i++) {
        struct v4l2_fmtdesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.index = i;
        desc.type = type;

        if (xioctl(fd, VIDIOC_ENUM_FMT, &desc) == -1) {
            break; /* EINVAL marks the end of the list */
        }

        int pixfmt = v4l2_fourcc_to_pixfmt(desc.pixelformat);
        if (pixfmt >= 0) {
            *pixfmt_mask |= TKL_CAMERA_V4L2_PIXFMT_BIT(pixfmt);
        }
    }

    close(fd);
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_open(TKL_CAMERA_V4L2_HANDLE_T *hdl, const TKL_CAMERA_V4L2_CFG_T *cfg)
{
    if (hdl == NULL || cfg == NULL || cfg->devnode == NULL) {
        return OPRT_INVALID_PARM;
    }

    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)calloc(1, sizeof(tkl_v4l2_ctx_t));
    if (!ctx) {
        return OPRT_MALLOC_FAILED;
    }

    ctx->fd = -1;
    ctx->width = cfg->width;
    ctx->height = cfg->height;
    ctx->fps = cfg->fps;
    ctx->buffer_count = (cfg->buffer_count == 0) ? 4 : cfg->buffer_count;

    switch (cfg->pixfmt) {
    case TKL_CAMERA_V4L2_PIXFMT_YUYV:
        ctx->fourcc = V4L2_PIX_FMT_YUYV;
        break;
    case TKL_CAMERA_V4L2_PIXFMT_MJPEG:
        ctx->fourcc = V4L2_PIX_FMT_MJPEG;
        break;
    case TKL_CAMERA_V4L2_PIXFMT_UYVY:
        ctx->fourcc = V4L2_PIX_FMT_UYVY;
        break;
    case TKL_CAMERA_V4L2_PIXFMT_NV12:
        ctx->fourcc = V4L2_PIX_FMT_NV12;
        break;
    case TKL_CAMERA_V4L2_PIXFMT_NV16:
        ctx->fourcc = V4L2_PIX_FMT_NV16;
        break;
    default:
        free(ctx);
        return OPRT_INVALID_PARM;
    }

    ctx->fd = open(cfg->devnode, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ctx->fd < 0) {
        free(ctx);
        return OPRT_COM_ERROR;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        close(ctx->fd);
        free(ctx);
        return OPRT_COM_ERROR;
    }

    /* device_caps describes this node; capabilities describes the whole device
     * and is only a fallback for drivers that do not fill device_caps. */
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;

    if (caps & V4L2_CAP_VIDEO_CAPTURE) {
        ctx->mplane = false;
        ctx->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        ctx->mplane = true;
        ctx->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        close(ctx->fd);
        free(ctx);
        return OPRT_NOT_SUPPORTED;
    }

    if (!(caps & V4L2_CAP_STREAMING)) {
        close(ctx->fd);
        free(ctx);
        return OPRT_NOT_SUPPORTED;
    }

    OPERATE_RET rt = v4l2_set_format_and_fps(ctx);
    if (rt != OPRT_OK) {
        close(ctx->fd);
        free(ctx);
        return rt;
    }

    *hdl = (TKL_CAMERA_V4L2_HANDLE_T)ctx;
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_get_fps(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t *fps)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;

    if (!ctx || !fps) {
        return OPRT_INVALID_PARM;
    }
    *fps = ctx->fps;
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_start(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || ctx->fd < 0) {
        return OPRT_INVALID_PARM;
    }
    if (ctx->streaming) {
        return OPRT_OK;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = ctx->buffer_count;
    req.type = ctx->buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
        return OPRT_COM_ERROR;
    }

    if (req.count < 2) {
        return OPRT_COM_ERROR;
    }

    ctx->buffers = (v4l2_buf_t *)calloc(req.count, sizeof(v4l2_buf_t));
    if (!ctx->buffers) {
        return OPRT_MALLOC_FAILED;
    }
    ctx->buffers_num = req.count;

    for (uint32_t i = 0; i < ctx->buffers_num; i++) {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[TKL_V4L2_MAX_PLANES];
        size_t map_len;
        off_t map_off;

        v4l2_buf_prepare(ctx, &buf, planes, i);

        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }

        if (ctx->mplane) {
            map_len = buf.m.planes[0].length;
            map_off = (off_t)buf.m.planes[0].m.mem_offset;
        } else {
            map_len = buf.length;
            map_off = (off_t)buf.m.offset;
        }

        ctx->buffers[i].length = map_len;
        ctx->buffers[i].start = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, map_off);
        if (ctx->buffers[i].start == MAP_FAILED) {
            ctx->buffers[i].start = NULL;
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            (void)tkl_camera_v4l2_stop(hdl);
            return OPRT_COM_ERROR;
        }
    }

    enum v4l2_buf_type type = ctx->buf_type;
    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
        (void)tkl_camera_v4l2_stop(hdl);
        return OPRT_COM_ERROR;
    }

    ctx->streaming = true;
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_stop(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || ctx->fd < 0) {
        return OPRT_INVALID_PARM;
    }

    if (ctx->streaming) {
        enum v4l2_buf_type type = ctx->buf_type;
        (void)xioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
        ctx->streaming = false;
    }

    if (ctx->buffers) {
        for (uint32_t i = 0; i < ctx->buffers_num; i++) {
            if (ctx->buffers[i].start && ctx->buffers[i].length) {
                (void)munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            }
        }
        free(ctx->buffers);
        ctx->buffers = NULL;
        ctx->buffers_num = 0;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_close(TKL_CAMERA_V4L2_HANDLE_T hdl)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }

    (void)tkl_camera_v4l2_stop(hdl);

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    free(ctx);
    return OPRT_OK;
}

OPERATE_RET tkl_camera_v4l2_dequeue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint8_t **data, uint32_t *len, uint32_t *index)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx || !data || !len || !index) {
        return OPRT_INVALID_PARM;
    }
    if (!ctx->streaming) {
        return OPRT_INVALID_PARM;
    }

    for (;;) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ctx->fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(ctx->fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) {
            if (errno == EINTR) {
                continue;
            }
            return OPRT_COM_ERROR;
        }
        if (r == 0) {
            return OPRT_TIMEOUT;
        }

        struct v4l2_buffer buf;
        struct v4l2_plane planes[TKL_V4L2_MAX_PLANES];

        v4l2_buf_prepare(ctx, &buf, planes, 0);

        if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) {
                continue;
            }
            return OPRT_COM_ERROR;
        }

        if (buf.index >= ctx->buffers_num) {
            return OPRT_COM_ERROR;
        }

        *data = (uint8_t *)ctx->buffers[buf.index].start;
        *len = ctx->mplane ? buf.m.planes[0].bytesused : buf.bytesused;
        *index = buf.index;
        return OPRT_OK;
    }
}

OPERATE_RET tkl_camera_v4l2_queue(TKL_CAMERA_V4L2_HANDLE_T hdl, uint32_t index)
{
    tkl_v4l2_ctx_t *ctx = (tkl_v4l2_ctx_t *)hdl;
    if (!ctx) {
        return OPRT_INVALID_PARM;
    }
    if (index >= ctx->buffers_num) {
        return OPRT_INVALID_PARM;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[TKL_V4L2_MAX_PLANES];

    v4l2_buf_prepare(ctx, &buf, planes, index);

    if (ctx->mplane) {
        /* Re-queueing needs the plane length back, QUERYBUF filled it once. */
        buf.m.planes[0].length = (uint32_t)ctx->buffers[index].length;
    }

    if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}
