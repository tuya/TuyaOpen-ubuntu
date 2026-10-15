/**
 * @file tkl_kv.c
 * @brief tal_kv 平台 ops 卡：Linux 用 littlefs + AES-128-CBC 做加密键值存储
 *
 * 由旧 third_party/tal_kv/src/tal_kv.c（tuyaopen-core 8fa1d6a^）改造：删掉
 * ENABLE_FILE_SYSTEM 分支（只留 littlefs）、serialize（kv_serialize.c）、
 * CLI（tal_kv_cmd）与 tal_lfs_get；kv_storage_* 从 tal_fs 门面改为直调 lfs API。
 *
 * 介质：platform（TuyaOpen-ubuntu）的 tkl_flash（linux 实现为文件模拟
 * ./tuyadb/tuyadb）的 UF 分区挂 littlefs；每条记录 = 明文经 AES-128-CBC 加密
 * 后的密文，IV/密钥在 init 时由 seed/key 各经 SHA256 派生 16 字节，读回时逐字节
 * 校验 PKCS7 填充。写/读/删全程持 s_kv_mutex（平台 tkl_mutex），加解密在锁外做。
 *
 * 平台 ops 卡：tal_kv_ops_platform 由本文件组装，core 的 tal_kv.c 经 extern 引用。
 * 移植其它平台时替换本文件（NVS 类平台直接映射芯片 KV 即可）。
 *
 * 零 core 依赖（分层约束：port 只向下依赖平台层，不 include core 头、不调 core 函数）：
 *  - 日志 → 平台 tkl_log_output（tkl_output.h）；内存 → tkl_system_malloc/free
 *    （tkl_memory.h）；互斥 → tkl_mutex_*（tkl_mutex.h）；
 *  - 密码学（AES-128-CBC / SHA-256）**在本文件内联实现**（OpenSSL libcrypto 后端，
 *    见下方"本文件内联的密码学原语"段）：旧实现调 core 的 tal_security 属向上调用，
 *    已改本层实现，**字节兼容**（换实现后仍能解既有 kv 数据）。
 *
 * 注意：lfs_util.h 经 -DLFS_CONFIG=lfs_config.h（CMake 注入）取 lfs_config.h。
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */
#include "tal_kv_ops.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include "tkl_memory.h"
#include "tkl_mutex.h"
#include "tkl_output.h"

#include "tkl_flash.h"

#include "lfs.h"

/* ===== 日志：平台 tkl_log_output（tkl_output.h） =====
 * 平台 Linux 弱实现只输出首参（printf("%s", str)，不消费可变参），故本文件先
 * vsnprintf 成串再交给平台输出；调用点的格式串自带行尾 '\n'。 */
static void __kv_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    tkl_log_output(buf);
}

/* ===== flash 介质：platform（TuyaOpen-ubuntu）的 tkl_flash 实现
 *
 * 分区地址与后备文件由 tkl_flash_get_one_type_info(TUYA_FLASH_TYPE_UF, ...)
 * 提供（linux 实现为文件模拟 ./tuyadb/tuyadb，首次打开时创建并预填 0xFF）；
 * 本文件的 __flash_* 仅转调 tkl_flash_read/write/erase。
 * ===== */
#define FLASH_PARTITION_SZ (1 << 12) /* 4KB，littlefs 的 block/read/prog 粒度 */

static TUYA_FLASH_BASE_INFO_T s_flash_info;

/* flash 文件的打开/创建/预填由 platform 的 tkl_flash 负责
 * （kv_storage_init 里经 tkl_flash_get_one_type_info 触发）。 */

static OPERATE_RET __flash_read(uint32_t addr, uint8_t *dst, uint32_t size)
{
    return tkl_flash_read(addr, dst, size);
}

static OPERATE_RET __flash_write(uint32_t addr, const uint8_t *src, uint32_t size)
{
    return tkl_flash_write(addr, src, size);
}

/* 擦除转调 platform 的 tkl_flash_erase（linux 文件实现为空操作；
 * 真 flash 平台由平台的块擦除命令实现） */
static OPERATE_RET __flash_erase(uint32_t addr, uint32_t size)
{
    return tkl_flash_erase(addr, size);
}

static lfs_t s_lfs;
static lfs_size_t s_lfs_flash_addr;

static tal_kv_cfg_t s_lfs_kv_cfg;
static TKL_MUTEX_HANDLE s_lfs_mutex;
static BOOL_T s_lfs_mounted = FALSE;

static int user_provided_block_device_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                                           lfs_size_t size)
{
    OPERATE_RET ret = __flash_read(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                                           const void *buffer, lfs_size_t size)
{
    OPERATE_RET ret = __flash_write(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_erase(const struct lfs_config *c, lfs_block_t block)
{
    OPERATE_RET ret = __flash_erase(s_lfs_flash_addr + c->block_size * block, c->block_size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

/* Storage helpers: callers hold s_lfs_mutex for read, write, and remove. */
static OPERATE_RET kv_storage_init(void)
{
    /* lfs 内部持有 cfg 指针（lfs->cfg），配置结构须比挂载活得久 */
    static struct lfs_config lfs_cfg;
    int result;

    if (OPRT_OK != tkl_flash_get_one_type_info(TUYA_FLASH_TYPE_UF, &s_flash_info)) {
        return OPRT_COM_ERROR;
    }

    s_lfs_flash_addr = s_flash_info.partition[0].start_addr;
    memset(&lfs_cfg, 0, sizeof(lfs_cfg));
    lfs_cfg.read = user_provided_block_device_read;
    lfs_cfg.prog = user_provided_block_device_prog;
    lfs_cfg.erase = user_provided_block_device_erase;
    lfs_cfg.sync = user_provided_block_device_sync;
    lfs_cfg.read_size = FLASH_PARTITION_SZ;
    lfs_cfg.prog_size = FLASH_PARTITION_SZ;
    lfs_cfg.block_size = FLASH_PARTITION_SZ;
    lfs_cfg.block_count = s_flash_info.partition[0].size / FLASH_PARTITION_SZ;
    lfs_cfg.cache_size = FLASH_PARTITION_SZ;
    /* 原样恢复自旧实现：公式为笔误（(8 - block_count/8) 使结果恒为 8）。该值是
     * littlefs 的内存前瞻缓冲——不落盘、不影响磁盘布局，改大改小只影响内存占用
     * 与分配器扫描窗口；恒为 8 经实测可工作，有意保留原样 */
    lfs_cfg.lookahead_size = lfs_cfg.block_count / 8 + (8 - (lfs_cfg.block_count / 8));
    lfs_cfg.block_cycles = 500;

    result = lfs_mount(&s_lfs, &lfs_cfg);
    if (LFS_ERR_OK != result) {
        result = lfs_format(&s_lfs, &lfs_cfg);
        if (LFS_ERR_OK == result) {
            result = lfs_mount(&s_lfs, &lfs_cfg);
        }
    }

    return (LFS_ERR_OK == result) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET kv_storage_write(const char *key, const uint8_t *data, uint32_t length)
{
    lfs_file_t file;
    int result;
    int sync_result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (LFS_ERR_OK != result) {
        __kv_log("lfs open %s failed: %d\n", key, result);
        return OPRT_FILE_OPEN_FAILED;
    }

    result = lfs_file_write(&s_lfs, &file, data, length);
    sync_result = lfs_file_sync(&s_lfs, &file);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)length || LFS_ERR_OK != sync_result || LFS_ERR_OK != close_result) {
        __kv_log("kv write failed write:%d sync:%d close:%d\n", result, sync_result, close_result);
        return OPRT_KVS_WR_FAIL;
    }

    return OPRT_OK;
}

static OPERATE_RET kv_storage_read(const char *key, uint8_t **data, uint32_t *length)
{
    lfs_file_t file;
    lfs_soff_t stored_length;
    int result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_RDONLY);
    if (LFS_ERR_OK != result) {
        return (LFS_ERR_NOENT == result) ? OPRT_NOT_FOUND : OPRT_FILE_OPEN_FAILED;
    }

    stored_length = lfs_file_size(&s_lfs, &file);
    if (stored_length < 0) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }
    if (0 == stored_length) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_NOT_FOUND;
    }
    if (stored_length % 16 != 0) {
        __kv_log("invalid encrypted KV length %d for %s\n", (int)stored_length, key);
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }

    *data = tkl_system_malloc((size_t)stored_length + 1U);
    if (NULL == *data) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_MALLOC_FAILED;
    }

    result = lfs_file_read(&s_lfs, &file, *data, (lfs_size_t)stored_length);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)stored_length || LFS_ERR_OK != close_result) {
        __kv_log("kv read failed read:%d close:%d\n", result, close_result);
        tkl_system_free(*data);
        *data = NULL;
        return OPRT_KVS_RD_FAIL;
    }

    *length = (uint32_t)stored_length;
    return OPRT_OK;
}

static OPERATE_RET kv_storage_remove(const char *key)
{
    int result = lfs_remove(&s_lfs, key);

    /* 契约（tal_kv.h）：键不存在视为成功 */
    return (LFS_ERR_OK == result || LFS_ERR_NOENT == result) ? OPRT_OK : OPRT_COM_ERROR;
}

/* ===== 本文件内联的密码学原语（AES-128-CBC / SHA-256，OpenSSL libcrypto 后端）
 *
 * 分层约束：port 只向下依赖（平台 / 第三方库），不得调用 tuyaopen-core。旧实现
 * 直接调 core 的 tal_aes128_cbc_encode/decode、tal_sha256_ret（向上调用），此处按
 * 同一语义在本层实现，**字节兼容**（换实现后仍能解既有 kv 数据）：
 *  - encode：分配 len+16 → PKCS#7 填充（len%16==0 补整块）→ CBC 原地加密；
 *  - decode：分配 len+1 → CBC 原地解密 → 末尾补 '\0'（去填充由调用方做）；
 *  - sha256：标准 SHA-256（core 的 tal_sha256_ret(..., is224=0) 等价）。
 * 分配走平台 tkl_system_malloc（与卡 .malloc/.free 同源——core 的 tal_free 经
 * api ops 卡即 tkl_system_free，调用者可直接释放）。
 * =========================================================================== */

#define KV_AES_BLOCK_LEN 16

/* PKCS#7 填充：返回填充后长度（len%16==0 时补整块 16） */
static uint32_t __kv_add_pkcs(uint8_t *p, uint32_t len)
{
    uint32_t cz = KV_AES_BLOCK_LEN - (len % KV_AES_BLOCK_LEN);

    memset(p + len, (int)cz, cz);
    return len + cz;
}

/* 单次 AES-128-CBC（原地；不启用 EVP 自带填充，填充语义由调用方控制） */
static OPERATE_RET __kv_aes128_cbc(uint8_t *buf, uint32_t len, const uint8_t *key, uint8_t *iv, int enc)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outl = 0;
    int tmpl = 0;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (NULL == ctx) {
        return OPRT_MALLOC_FAILED;
    }
    if (1 != EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv, enc)) {
        goto exit;
    }
    if (1 != EVP_CIPHER_CTX_set_padding(ctx, 0)) {
        goto exit;
    }
    if (1 != EVP_CipherUpdate(ctx, buf, &outl, buf, (int)len)) {
        goto exit;
    }
    if (1 != EVP_CipherFinal_ex(ctx, buf + outl, &tmpl)) {
        goto exit;
    }
    ret = OPRT_OK;

exit:
    EVP_CIPHER_CTX_free(ctx);
    return ret;
}

static OPERATE_RET __kv_sha256_ret(const uint8_t *input, size_t ilen, uint8_t output[32])
{
    if (NULL == input || NULL == output) {
        return OPRT_INVALID_PARM;
    }

    if (NULL == SHA256(input, ilen, output)) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static OPERATE_RET __kv_aes128_cbc_encode(uint8_t *data, uint32_t len, uint8_t *key, uint8_t *iv, uint8_t **ec_data,
                                          uint32_t *ec_len)
{
    if (NULL == data || 0 == len || NULL == key || NULL == iv || NULL == ec_data || NULL == ec_len) {
        return OPRT_INVALID_PARM;
    }

    *ec_data = (uint8_t *)tkl_system_malloc((SIZE_T)len + KV_AES_BLOCK_LEN);
    if (NULL == *ec_data) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(*ec_data, data, len);
    *ec_len = __kv_add_pkcs(*ec_data, len);

    if (OPRT_OK != __kv_aes128_cbc(*ec_data, *ec_len, key, iv, 1)) {
        tkl_system_free(*ec_data);
        *ec_data = NULL;
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static OPERATE_RET __kv_aes128_cbc_decode(uint8_t *data, uint32_t len, uint8_t *key, uint8_t *iv, uint8_t **dec_data,
                                          uint32_t *dec_len)
{
    if (NULL == data || 0 == len || NULL == key || NULL == iv || NULL == dec_data || NULL == dec_len) {
        return OPRT_INVALID_PARM;
    }

    *dec_len = len;
    *dec_data = (uint8_t *)tkl_system_malloc((SIZE_T)len + 1);
    if (NULL == *dec_data) {
        return OPRT_MALLOC_FAILED;
    }
    memcpy(*dec_data, data, len);
    (*dec_data)[len] = 0;

    if (OPRT_OK != __kv_aes128_cbc(*dec_data, len, key, iv, 0)) {
        tkl_system_free(*dec_data);
        *dec_data = NULL;
        return OPRT_COM_ERROR;
    }
    (*dec_data)[len] = 0;
    return OPRT_OK;
}

static OPERATE_RET kv_encrypt(const uint8_t *value, size_t length, uint8_t **encrypted, uint32_t *encrypted_length)
{
    uint8_t iv[16];

    if (length > UINT32_MAX - 16U) {
        return OPRT_INVALID_PARM;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    return __kv_aes128_cbc_encode((uint8_t *)value, (uint32_t)length, (uint8_t *)s_lfs_kv_cfg.key, iv, encrypted,
                                  encrypted_length);
}

static OPERATE_RET kv_decrypt(uint8_t *encrypted, uint32_t encrypted_length, uint8_t **value, size_t *value_length)
{
    uint8_t iv[16];
    uint8_t *decrypted = NULL;
    uint32_t decrypted_length = 0;
    uint8_t padding_length;
    uint32_t i;
    OPERATE_RET result;

    if (0 == encrypted_length || encrypted_length % 16U != 0U) {
        return OPRT_KVS_RD_FAIL;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    result = __kv_aes128_cbc_decode(encrypted, encrypted_length, (uint8_t *)s_lfs_kv_cfg.key, iv, &decrypted,
                                    &decrypted_length);
    if (OPRT_OK != result) {
        return result;
    }

    if (0 == decrypted_length || decrypted_length % 16U != 0U) {
        tkl_system_free(decrypted);
        return OPRT_KVS_RD_FAIL;
    }

    padding_length = decrypted[decrypted_length - 1U];
    if (0 == padding_length || padding_length > 16U || padding_length >= decrypted_length) {
        tkl_system_free(decrypted);
        return OPRT_KVS_RD_FAIL;
    }
    for (i = 0; i < padding_length; ++i) {
        if (decrypted[decrypted_length - 1U - i] != padding_length) {
            tkl_system_free(decrypted);
            return OPRT_KVS_RD_FAIL;
        }
    }

    *value_length = decrypted_length - padding_length;
    decrypted[*value_length] = 0;
    *value = decrypted;
    return OPRT_OK;
}

static OPERATE_RET __kv_init(tal_kv_cfg_t *kv_cfg)
{
    uint8_t sha256_ret[32];
    OPERATE_RET result;

    if (NULL == kv_cfg) {
        return OPRT_INVALID_PARM;
    }

    /* 密钥派生：每次 init 都按传入配置刷新（旧实现语义） */
    memset(&s_lfs_kv_cfg, 0, sizeof(s_lfs_kv_cfg));
    __kv_sha256_ret((const uint8_t *)kv_cfg->seed, TAL_LV_KEY_LEN, sha256_ret);
    memcpy(s_lfs_kv_cfg.seed, sha256_ret, TAL_LV_KEY_LEN);
    __kv_sha256_ret((const uint8_t *)kv_cfg->key, TAL_LV_KEY_LEN, sha256_ret);
    memcpy(s_lfs_kv_cfg.key, sha256_ret, TAL_LV_KEY_LEN);

    if (NULL == s_lfs_mutex) {
        result = tkl_mutex_create_init(&s_lfs_mutex);
        if (OPRT_OK != result) {
            return result;
        }
    }

    /* 重复 init 幂等：介质已挂载就不再 mount（旧实现重复 mount 会泄漏 lfs 缓冲，
     * 且 mount 失败路径上的 format 会把已有数据抹掉） */
    if (TRUE == s_lfs_mounted) {
        return OPRT_OK;
    }

    result = kv_storage_init();
    if (OPRT_OK != result) {
        return result;
    }
    s_lfs_mounted = TRUE;

    return OPRT_OK;
}

static OPERATE_RET __kv_set(const char *key, const uint8_t *value, size_t length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    if (NULL == key || NULL == value || 0 == length) {
        return OPRT_INVALID_PARM;
    }
    /* 未 init（介质未挂载）时不得触碰 lfs：lfs->cfg 为空会崩 */
    if (FALSE == s_lfs_mounted) {
        __kv_log("kv not mounted, set %s rejected\n", key);
        return OPRT_COM_ERROR;
    }

    __kv_log("key:%s, len %u\n", key, (unsigned int)length);
    result = kv_encrypt(value, length, &encrypted, &encrypted_length);
    if (OPRT_OK != result) {
        __kv_log("key %s encrypt failed: %d\n", key, result);
        return result;
    }

    tkl_mutex_lock(s_lfs_mutex);
    result = kv_storage_write(key, encrypted, encrypted_length);
    tkl_mutex_unlock(s_lfs_mutex);
    tkl_system_free(encrypted);
    return result;
}

static OPERATE_RET __kv_get(const char *key, uint8_t **value, size_t *length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    /* 契约（tal_kv.h）：失败时 *value 置 NULL、*length 置 0。先于参数校验归零，
     * 保证 port 被直接调用（不经济传的 tal_kv.c）也守住承诺 */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }

    if (NULL == key || NULL == value || NULL == length) {
        return OPRT_INVALID_PARM;
    }
    /* 未 init（介质未挂载）时不得触碰 lfs：lfs->cfg 为空会崩 */
    if (FALSE == s_lfs_mounted) {
        __kv_log("kv not mounted, get %s rejected\n", key);
        return OPRT_COM_ERROR;
    }

    tkl_mutex_lock(s_lfs_mutex);
    result = kv_storage_read(key, &encrypted, &encrypted_length);
    tkl_mutex_unlock(s_lfs_mutex);
    if (OPRT_OK != result) {
        return result;
    }

    /* kv_decrypt 内部 tkl_system_malloc 的明文缓冲（末尾已补 '\0'）直接交给
     * 调用者，由调用者 tal_free 释放（core 的 tal_free 经 api ops 卡即
     * tkl_system_free，与分配同源）；密文缓冲在此处释放 */
    result = kv_decrypt(encrypted, encrypted_length, value, length);
    tkl_system_free(encrypted);
    if (OPRT_OK != result) {
        __kv_log("key %s decrypt failed: %d\n", key, result);
    }

    return result;
}

static OPERATE_RET __kv_del(const char *key)
{
    OPERATE_RET result;

    if (NULL == key) {
        return OPRT_INVALID_PARM;
    }
    /* 未 init（介质未挂载）时不得触碰 lfs：lfs->cfg 为空会崩 */
    if (FALSE == s_lfs_mounted) {
        __kv_log("kv not mounted, del %s rejected\n", key);
        return OPRT_COM_ERROR;
    }

    tkl_mutex_lock(s_lfs_mutex);
    result = kv_storage_remove(key);
    tkl_mutex_unlock(s_lfs_mutex);
    return result;
}

/* ---------------------------------------------------------------------------
 * 平台 ops 卡：由本文件组装，core 的 tal_kv.c 经 extern 引用完成分发。
 * 移植新平台时提供同名卡与 4 个 port_kv_* 实现即可。
 * --------------------------------------------------------------------------- */
TAL_KV_OPS_T tal_kv_ops_platform = {
    .init = __kv_init,
    .set = __kv_set,
    .get = __kv_get,
    .del = __kv_del,
};
