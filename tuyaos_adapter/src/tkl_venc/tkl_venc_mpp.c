/**
 * @file tkl_venc_mpp.c
 * @brief Hardware video encoder on Rockchip SoCs, via librockchip_mpp.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#define MODULE_TAG "tkl_venc_mpp"

#include "media/tkl_venc_mpp.h"
#include "tal_log.h"
#include "tuya_error_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_mpi.h"
#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_venc_cfg.h"
#include "rk_venc_rc.h"

/* The encoder wants 16-aligned strides; the capture side hands us a tightly
 * packed frame, so the copy below pads rather than assuming they match. */
#define VENC_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define VENC_STRIDE_ALIGN 16

typedef struct {
    MppCtx ctx;
    MppApi *mpi;
    MppBufferGroup buf_grp;
    MppBuffer frame_buf;

    uint32_t width;
    uint32_t height;
    uint32_t hor_stride;
    uint32_t ver_stride;
    size_t frame_size;
    TKL_VENC_CODEC_E codec;

    /* Encoded data is copied out of the MppPacket so the packet can be released
     * immediately and the caller gets a pointer with an obvious lifetime. */
    uint8_t *out_buf;
    size_t out_cap;

    /* Kept so rate control can be re-applied when the real frame rate is
     * measured, without the caller having to hand back the whole config. */
    TKL_VENC_MPP_CFG_T cfg;
} tkl_venc_mpp_ctx_t;

/**
 * @brief Copy a tightly packed NV12 frame into a stride-padded MPP buffer.
 *
 * Y is height rows of width bytes, UV is height/2 rows of width bytes. When the
 * encoder stride differs from the frame width (1080 becomes 1088 rows, for
 * example) a straight memcpy shears the image, so copy row by row.
 */
static void venc_copy_nv12(tkl_venc_mpp_ctx_t *c, const uint8_t *src, uint8_t *dst)
{
    uint32_t row;
    const uint8_t *s = src;
    uint8_t *d = dst;

    if (c->hor_stride == c->width && c->ver_stride == c->height) {
        memcpy(dst, src, (size_t)c->width * c->height * 3 / 2);
        return;
    }

    for (row = 0; row < c->height; row++) {
        memcpy(d, s, c->width);
        s += c->width;
        d += c->hor_stride;
    }

    /* UV plane starts at ver_stride rows into the destination. */
    d = dst + (size_t)c->hor_stride * c->ver_stride;
    for (row = 0; row < c->height / 2; row++) {
        memcpy(d, s, c->width);
        s += c->width;
        d += c->hor_stride;
    }
}

/**
 * @brief Set one encoder config key, and say so when MPP will not take it.
 *
 * mpp_enc_cfg_set_s32() returns an error for a key this build of MPP does not
 * know, and every call site here used to discard it. A tuning knob that was
 * never applied then looks exactly like a knob that did not help.
 */
static void venc_cfg_set(MppEncCfg cfg, const char *key, RK_S32 val)
{
    MPP_RET ret = mpp_enc_cfg_set_s32(cfg, key, val);

    if (ret != MPP_OK) {
        PR_WARN("venc cfg key '%s'=%d rejected by MPP (ret %d)", key, val, ret);
    }
}

static OPERATE_RET venc_apply_cfg(tkl_venc_mpp_ctx_t *c, const TKL_VENC_MPP_CFG_T *cfg)
{
    MppEncCfg enc_cfg = NULL;
    uint32_t fps = cfg->fps ? cfg->fps : 30;
    uint32_t gop = cfg->gop ? cfg->gop : fps * 2;
    uint32_t bps;

    if (cfg->bitrate_kbps) {
        bps = cfg->bitrate_kbps * 1000;
    } else {
        /* Rough default in the range Rockchip's own samples use. */
        bps = c->width * c->height / 8 * fps;
    }

    /* DBG: confirm the requested rate control actually reached MPP rather than
     * silently falling back to the resolution-derived default. */
    PR_DEBUG("DBG venc cfg: %ux%u fps=%u gop=%u bps=%u (%u kbps) mode=CBR", c->width, c->height, fps, gop, bps,
             bps / 1000);

    if (mpp_enc_cfg_init(&enc_cfg) != MPP_OK) {
        return OPRT_COM_ERROR;
    }

    if (c->mpi->control(c->ctx, MPP_ENC_GET_CFG, enc_cfg) != MPP_OK) {
        mpp_enc_cfg_deinit(enc_cfg);
        return OPRT_COM_ERROR;
    }

    venc_cfg_set(enc_cfg, "prep:width", (RK_S32)c->width);
    venc_cfg_set(enc_cfg, "prep:height", (RK_S32)c->height);
    venc_cfg_set(enc_cfg, "prep:hor_stride", (RK_S32)c->hor_stride);
    venc_cfg_set(enc_cfg, "prep:ver_stride", (RK_S32)c->ver_stride);
    venc_cfg_set(enc_cfg, "prep:format", MPP_FMT_YUV420SP);

    /* CBR keeps the bitrate predictable, which matters more than peak quality
     * when the stream has to fit a fixed P2P send budget. */
    venc_cfg_set(enc_cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    venc_cfg_set(enc_cfg, "rc:bps_target", (RK_S32)bps);
    venc_cfg_set(enc_cfg, "rc:bps_max", (RK_S32)(bps * 17 / 16));
    venc_cfg_set(enc_cfg, "rc:bps_min", (RK_S32)(bps * 15 / 16));
    venc_cfg_set(enc_cfg, "rc:fps_in_flex", 0);
    venc_cfg_set(enc_cfg, "rc:fps_in_num", (RK_S32)fps);
    venc_cfg_set(enc_cfg, "rc:fps_in_denorm", 1);
    venc_cfg_set(enc_cfg, "rc:fps_out_flex", 0);
    venc_cfg_set(enc_cfg, "rc:fps_out_num", (RK_S32)fps);
    venc_cfg_set(enc_cfg, "rc:fps_out_denorm", 1);
    venc_cfg_set(enc_cfg, "rc:gop", (RK_S32)gop);

    /*
     * No I-frame overrides here, and rc:max_i_prop is not the knob for it.
     * Measured on RK3576: max_i_prop=40 / min_i_prop=10 are both accepted by
     * MPP - venc_cfg_set() reported no rejection - and change nothing. Over a
     * 50-frame run the I-frame still walked 27KB up to 82KB, the same ceiling
     * the untouched build reaches.
     *
     * What the same run did show is where the growth comes from: every
     * venc_apply_cfg() call resets it. Each bitrate change dropped the next
     * I-frame back to ~42KB, and it then climbed one step per GOP - 42, 49,
     * 59, 70, 81KB - until the next change. A link steady enough that rate
     * control stops moving is what lets it reach the top.
     *
     * 82KB over 307200 pixels is 2.08 bits each, which is a far finer I-frame
     * than this stream needs. That points at the quantiser floor, rc:qp_min_i,
     * as the thing to bound - raising it, not lowering it the way an earlier
     * attempt at 18 did.
     */
    if (c->codec == TKL_VENC_CODEC_H265) {
        venc_cfg_set(enc_cfg, "codec:type", MPP_VIDEO_CodingHEVC);
        /* MPP's HEVC defaults are Main profile at a level it derives from the
         * geometry, which is what we want; overriding level would only risk
         * declaring one the stream does not fit. */
    } else {
        venc_cfg_set(enc_cfg, "codec:type", MPP_VIDEO_CodingAVC);
        /* Baseline-compatible settings: no CABAC, no 8x8 transform. Keeps the
         * stream decodable by the widest set of phone decoders. */
        venc_cfg_set(enc_cfg, "h264:profile", 66);
        venc_cfg_set(enc_cfg, "h264:level", 40);
        venc_cfg_set(enc_cfg, "h264:cabac_en", 0);
        venc_cfg_set(enc_cfg, "h264:trans8x8", 0);
    }

    if (c->mpi->control(c->ctx, MPP_ENC_SET_CFG, enc_cfg) != MPP_OK) {
        mpp_enc_cfg_deinit(enc_cfg);
        return OPRT_COM_ERROR;
    }

    mpp_enc_cfg_deinit(enc_cfg);

    /* MPP's default emits VPS/SPS/PPS on the first frame only, so a viewer that
     * joins later gets an IDR it cannot decode. */
    MppEncHeaderMode hdr = MPP_ENC_HEADER_MODE_EACH_IDR;
    if (c->mpi->control(c->ctx, MPP_ENC_SET_HEADER_MODE, &hdr) != MPP_OK) {
        PR_WARN("venc header mode EACH_IDR rejected; late joiners may not decode");
    }

    return OPRT_OK;
}

OPERATE_RET tkl_venc_mpp_open(TKL_VENC_MPP_HANDLE_T *hdl, const TKL_VENC_MPP_CFG_T *cfg)
{
    if (hdl == NULL || cfg == NULL || cfg->width == 0 || cfg->height == 0) {
        return OPRT_INVALID_PARM;
    }

    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)calloc(1, sizeof(*c));
    if (c == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    c->width = cfg->width;
    c->height = cfg->height;
    c->hor_stride = VENC_ALIGN(cfg->width, VENC_STRIDE_ALIGN);
    c->ver_stride = VENC_ALIGN(cfg->height, VENC_STRIDE_ALIGN);
    c->frame_size = (size_t)c->hor_stride * c->ver_stride * 3 / 2;
    c->codec = cfg->codec;
    c->cfg = *cfg;

    if (mpp_create(&c->ctx, &c->mpi) != MPP_OK) {
        goto err;
    }
    if (mpp_init(c->ctx, MPP_CTX_ENC,
                 c->codec == TKL_VENC_CODEC_H265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC) != MPP_OK) {
        goto err;
    }
    PR_NOTICE("venc %ux%u %s", c->width, c->height, c->codec == TKL_VENC_CODEC_H265 ? "H.265" : "H.264");
    if (venc_apply_cfg(c, cfg) != OPRT_OK) {
        goto err;
    }
    if (mpp_buffer_group_get_internal(&c->buf_grp, MPP_BUFFER_TYPE_DRM) != MPP_OK) {
        goto err;
    }
    if (mpp_buffer_get(c->buf_grp, &c->frame_buf, c->frame_size) != MPP_OK) {
        goto err;
    }

    *hdl = (TKL_VENC_MPP_HANDLE_T)c;
    return OPRT_OK;

err:
    (void)tkl_venc_mpp_close((TKL_VENC_MPP_HANDLE_T)c);
    return OPRT_COM_ERROR;
}

OPERATE_RET tkl_venc_mpp_encode(TKL_VENC_MPP_HANDLE_T hdl, const uint8_t *nv12, uint32_t len, uint8_t **out,
                                uint32_t *out_len, BOOL_T *is_keyframe)
{
    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)hdl;
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (c == NULL || nv12 == NULL || out == NULL || out_len == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (len < (size_t)c->width * c->height * 3 / 2) {
        return OPRT_INVALID_PARM;
    }

    *out = NULL;
    *out_len = 0;
    if (is_keyframe) {
        *is_keyframe = FALSE;
    }

    venc_copy_nv12(c, nv12, (uint8_t *)mpp_buffer_get_ptr(c->frame_buf));

    if (mpp_frame_init(&frame) != MPP_OK) {
        return OPRT_COM_ERROR;
    }
    mpp_frame_set_width(frame, c->width);
    mpp_frame_set_height(frame, c->height);
    mpp_frame_set_hor_stride(frame, c->hor_stride);
    mpp_frame_set_ver_stride(frame, c->ver_stride);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_buffer(frame, c->frame_buf);
    mpp_frame_set_eos(frame, 0);

    if (c->mpi->encode_put_frame(c->ctx, frame) != MPP_OK) {
        mpp_frame_deinit(&frame);
        return OPRT_COM_ERROR;
    }
    mpp_frame_deinit(&frame);

    if (c->mpi->encode_get_packet(c->ctx, &packet) != MPP_OK) {
        return OPRT_COM_ERROR;
    }
    if (packet == NULL) {
        /* Frame accepted, nothing to emit yet. */
        return OPRT_RESOURCE_NOT_READY;
    }

    size_t plen = mpp_packet_get_length(packet);
    void *pdata = mpp_packet_get_pos(packet);

    if (plen && pdata) {
        if (plen > c->out_cap) {
            uint8_t *nb = (uint8_t *)realloc(c->out_buf, plen);
            if (nb == NULL) {
                mpp_packet_deinit(&packet);
                return OPRT_MALLOC_FAILED;
            }
            c->out_buf = nb;
            c->out_cap = plen;
        }
        memcpy(c->out_buf, pdata, plen);
        *out = c->out_buf;
        *out_len = (uint32_t)plen;

        if (is_keyframe) {
            MppMeta meta = mpp_packet_get_meta(packet);
            RK_S32 intra = 0;
            if (meta && mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &intra) == MPP_OK) {
                *is_keyframe = intra ? TRUE : FALSE;
            }
            /* DBG one-shot: hex-dump the SPS/PPS/IDR header bytes MPP actually
             * emitted for cfg->width x cfg->height. Ground truth for the real
             * encoded geometry, independent of anything declared to the App -
             * decodes whether a distorted picture is an encoder crop/stride
             * problem or purely an App-side rendering issue. */
            static BOOL_T s_sps_dumped = FALSE;
            if (*is_keyframe && !s_sps_dumped) {
                s_sps_dumped = TRUE;
                size_t dump_len = plen < 48 ? plen : 48;
                char hex[48 * 3 + 1];
                size_t i;
                for (i = 0; i < dump_len; i++) {
                    snprintf(hex + i * 3, 4, "%02x ", ((uint8_t *)pdata)[i]);
                }
                PR_DEBUG("DBG encoder cfg=%ux%u stride=%ux%u first %zu bytes: %s", c->width, c->height,
                         c->hor_stride, c->ver_stride, dump_len, hex);
            }
        }
        rt = OPRT_OK;
    } else {
        rt = OPRT_RESOURCE_NOT_READY;
    }

    mpp_packet_deinit(&packet);
    return rt;
}

OPERATE_RET tkl_venc_mpp_set_fps(TKL_VENC_MPP_HANDLE_T hdl, uint32_t fps)
{
    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)hdl;
    TKL_VENC_MPP_CFG_T cfg;

    if (c == NULL || c->mpi == NULL || fps == 0) {
        return OPRT_INVALID_PARM;
    }
    if (c->cfg.fps == fps) {
        return OPRT_OK;
    }

    cfg = c->cfg;
    /* Keep the I-frame interval the caller asked for in seconds, not in
     * frames, so a rate correction does not silently change it. */
    if (c->cfg.gop && c->cfg.fps) {
        cfg.gop = c->cfg.gop * fps / c->cfg.fps;
    }
    cfg.fps = fps;

    if (venc_apply_cfg(c, &cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    c->cfg = cfg;
    PR_DEBUG("venc rate control moved to %u fps, gop %u", cfg.fps, cfg.gop);
    return OPRT_OK;
}

OPERATE_RET tkl_venc_mpp_set_bitrate(TKL_VENC_MPP_HANDLE_T hdl, uint32_t kbps)
{
    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)hdl;
    TKL_VENC_MPP_CFG_T cfg;

    if (c == NULL || c->mpi == NULL || kbps == 0) {
        return OPRT_INVALID_PARM;
    }
    if (c->cfg.bitrate_kbps == kbps) {
        return OPRT_OK;
    }

    cfg = c->cfg;
    cfg.bitrate_kbps = kbps;
    if (venc_apply_cfg(c, &cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    c->cfg = cfg;
    PR_DEBUG("venc bitrate now %u kbps", kbps);
    return OPRT_OK;
}

OPERATE_RET tkl_venc_mpp_request_idr(TKL_VENC_MPP_HANDLE_T hdl)
{
    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)hdl;

    if (c == NULL || c->mpi == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (c->mpi->control(c->ctx, MPP_ENC_SET_IDR_FRAME, NULL) != MPP_OK) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

OPERATE_RET tkl_venc_mpp_close(TKL_VENC_MPP_HANDLE_T hdl)
{
    tkl_venc_mpp_ctx_t *c = (tkl_venc_mpp_ctx_t *)hdl;

    if (c == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (c->frame_buf) {
        mpp_buffer_put(c->frame_buf);
        c->frame_buf = NULL;
    }
    if (c->buf_grp) {
        mpp_buffer_group_put(c->buf_grp);
        c->buf_grp = NULL;
    }
    if (c->ctx) {
        if (c->mpi) {
            c->mpi->reset(c->ctx);
        }
        mpp_destroy(c->ctx);
        c->ctx = NULL;
    }
    if (c->out_buf) {
        free(c->out_buf);
        c->out_buf = NULL;
    }
    free(c);
    return OPRT_OK;
}
