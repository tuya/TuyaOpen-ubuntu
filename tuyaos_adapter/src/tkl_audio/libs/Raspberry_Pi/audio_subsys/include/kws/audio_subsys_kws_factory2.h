/*
 * @Description: file content
 * @FilePath: audio_subsys_kws_factory2.h
 * @Version: v0.0.0
 * @Date: 2025-02-19 16:14:37
 * @LastEditTime: 2025-02-19 18:08:45
 */
#ifndef __AUDIO_SUBSYS_KWS_FACTORY2_H__
#define __AUDIO_SUBSYS_KWS_FACTORY2_H__

#ifdef __cplusplus
extern "C" {
#endif

// #include "audio_subsys.h"

typedef int(*audio_subsys_callback_t)(void *data, int dlen, int flag, void *userdata);
typedef struct audio_subsys_module {
    void *self;
    char *name;
    int (*create)(void **self, void *cfg);
    int (*destroy)(void *self);
    int (*set_callback)(void *self, audio_subsys_callback_t cb, void *userdata);
    int (*ioctl)(void *self, int cmd, void *arg);
    int (*process)(void *self, const void *in, int inlen, void *out, int outlen);
} audio_subsys_module_t;
extern audio_subsys_module_t audio_subsys_kws_factory2;

#define KWS_WINDOW_SIZE 30
#define KWS_WORDS 2

typedef struct kws_factory2_config_t {
    /* multi kws factory config filed */
    int     interpreter_type;   ///< 0: ort, 1: ncnn, 2: mnn
    int     chunk_size;         ///< dstcn+mnn: 300ms, mdtc+ncnn: 2000ms, fsmn ctc+mnn: 300ms. TODO: what? optimize！
    int     feats_size;         ///< dstcn+mnn: 30, fsmn ctc+mnn: 10, mdtc+ncnn: 200. TODO: what? optimize！

    int     sample_rate;
    int     interval;
    int     model_type;         ///< 0: dstcn maxpooling, 1: fsmn ctc, 2: mdtc maxpooling
    int     num_bins;           ///< dstcn: 40, fsmn ctc: 80, mdtc: 80.
    int     batch_size;         ///< dstcn+mnn: 10, fsmn ctc+mnn: 10, mdtc+ncnn: 10.
    float   threshold;
    char   *key_word;
    char   *model_path;
    char   *param_path;         ///< only for ncnn!
    char   *token_path;
} kws_factory2_config_t;

extern audio_subsys_module_t audio_subsys_kws_factory2;

int audio_subsys_kws_factory2_create(void **self, void *cfg);
int audio_subsys_kws_factory2_destroy(void *self);
int audio_subsys_kws_factory2_set_callback(void *self, audio_subsys_callback_t cb, void *userdata);
int audio_subsys_kws_factory2_ioctl(void *self, int cmd, void *arg);
int audio_subsys_kws_factory2_process(void *self, const void *in, int inlen, void *out, int outlen);

#ifdef __cplusplus
}
#endif

#endif /** ！__AUDIO_SUBSYS_KWS_FACTORY2_H__ */
