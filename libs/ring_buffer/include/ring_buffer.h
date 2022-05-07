#ifndef RINGBUFFER_H_INCLUDED
#define RINGBUFFER_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#if CHAR_BIT != 8
#error "This program run at must be CHAR_BIT == 8"
#endif

/* リングバッファ生成コンフィグ */
struct RingBufferConfig {
    size_t max_size; /* バッファサイズ */
    size_t max_required_size; /* 取り出し最大サイズ */
};

typedef enum RingBufferApiResult {
    RINGBUFFER_APIRESULT_OK = 0,
    RINGBUFFER_APIRESULT_INVALID_ARGUMENT,
    RINGBUFFER_APIRESULT_EXCEED_MAX_CAPACITY,
    RINGBUFFER_APIRESULT_EXCEED_MAX_REMAIN,
    RINGBUFFER_APIRESULT_EXCEED_MAX_REQUIRED,
    RINGBUFFER_APIRESULT_NG
} RingBufferApiResult;

struct RingBuffer;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* リングバッファ作成に必要なワークサイズ計算 */
int32_t RingBuffer_CalculateWorkSize(const struct RingBufferConfig *config);

/* リングバッファ作成 */
struct RingBuffer *RingBuffer_Create(const struct RingBufferConfig *config, void *work, int32_t work_size);

/* リングバッファ破棄 */
void RingBuffer_Destroy(struct RingBuffer *buffer);

/* リングバッファの内容をクリア */
void RingBuffer_Clear(struct RingBuffer *buffer);

/* リングバッファ内に残ったデータサイズ取得 */
size_t RingBuffer_GetRemainSize(const struct RingBuffer *buffer);

/* リングバッファ内の空き領域サイズ取得 */
size_t RingBuffer_GetCapacitySize(const struct RingBuffer *buffer);

/* データ挿入 */
RingBufferApiResult RingBuffer_Put(
        struct RingBuffer *buffer, const void *data, size_t size);

/* データ見るだけ（バッファの状態は更新されない） 注意）バッファが一周する前に使用しないと上書きされる */
RingBufferApiResult RingBuffer_Peek(
        struct RingBuffer *buffer, void **pdata, size_t required_size);

/* データ取得 注意）バッファが一周する前に使用しないと上書きされる */
RingBufferApiResult RingBuffer_Get(
        struct RingBuffer *buffer, void **pdata, size_t required_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RINGBUFFER_H_INCLUDED */
