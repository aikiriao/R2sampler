#include "ring_buffer.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* メモリアラインメント */
#define RINGBUFFER_ALIGNMENT 16
/* nの倍数への切り上げ */
#define RINGBUFFER_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 最小値の取得 */
#define RINGBUFFER_MIN(a,b) (((a) < (b)) ? (a) : (b))

/* リングバッファ */
struct RingBuffer {
    uint8_t *data; /* データ領域の先頭ポインタ データは8ビットデータ列と考える */
    size_t buffer_size; /* バッファデータサイズ */
    size_t max_required_size; /* 最大要求データサイズ */
    uint32_t read_pos; /* 読み出し位置 */
    uint32_t write_pos; /* 書き出し位置 */
};

/* リングバッファ作成に必要なワークサイズ計算 */
int32_t RingBuffer_CalculateWorkSize(const struct RingBufferConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }
    
    /* バッファサイズは要求サイズより大きい */
    if (config->max_size < config->max_required_size) {
        return -1;
    }

    work_size = sizeof(struct RingBuffer) + RINGBUFFER_ALIGNMENT;
    work_size += config->max_size + 1 + RINGBUFFER_ALIGNMENT;
    work_size += config->max_required_size;

    return work_size;
}

/* リングバッファ作成 */
struct RingBuffer *RingBuffer_Create(const struct RingBufferConfig *config, void *work, int32_t work_size)
{
    struct RingBuffer *buffer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((config == NULL) || (work == NULL) || (work_size < 0)) {
        return NULL;
    }

    if (work_size < RingBuffer_CalculateWorkSize(config)) {
        return NULL;
    }

    /* ハンドル領域割当 */
    work_ptr = (uint8_t *)RINGBUFFER_ROUNDUP((uintptr_t)work, RINGBUFFER_ALIGNMENT);
    buffer = (struct RingBuffer *)work_ptr;
    work_ptr += sizeof(struct RingBuffer);

    /* サイズを記録 */
    buffer->buffer_size = config->max_size + 1; /* バッファの位置関係を正しく解釈するため1要素分多く確保する（write_pos == read_pos のときデータが一杯なのか空なのか判定できない） */
    buffer->max_required_size = config->max_required_size;

    /* バッファ領域割当 */
    work_ptr = (uint8_t *)RINGBUFFER_ROUNDUP((uintptr_t)work_ptr, RINGBUFFER_ALIGNMENT);
    buffer->data = work_ptr;
    work_ptr += (buffer->buffer_size + config->max_required_size);

    /* バッファの内容をクリア */
    RingBuffer_Clear(buffer);

    return buffer;
}

/* リングバッファ破棄 */
void RingBuffer_Destroy(struct RingBuffer *buffer)
{
    /* 不定領域アクセス防止のため内容はクリア */
    RingBuffer_Clear(buffer);
}

/* リングバッファの内容をクリア */
void RingBuffer_Clear(struct RingBuffer *buffer)
{
    assert(buffer != NULL);

    /* データ領域を0埋め */
    memset(buffer->data, 0, buffer->buffer_size + buffer->max_required_size);

    /* バッファ参照位置を初期化 */
    buffer->read_pos = 0;
    buffer->write_pos = 0;
}

/* リングバッファ内に残ったデータサイズ取得 */
size_t RingBuffer_GetRemainSize(const struct RingBuffer *buffer)
{
    assert(buffer != NULL);

    if (buffer->read_pos > buffer->write_pos) {
        return buffer->buffer_size + buffer->write_pos - buffer->read_pos;
    }

    return buffer->write_pos - buffer->read_pos;
}

/* リングバッファ内の空き領域サイズ取得 */
size_t RingBuffer_GetCapacitySize(const struct RingBuffer *buffer)
{
    assert(buffer != NULL);
    assert(buffer->buffer_size >= RingBuffer_GetRemainSize(buffer));

    /* 実際に入るサイズはバッファサイズより1バイト少ない */
    return buffer->buffer_size - RingBuffer_GetRemainSize(buffer) - 1;
}

/* データ挿入 */
RingBufferApiResult RingBuffer_Put(
        struct RingBuffer *buffer, const void *data, size_t size)
{
    /* 引数チェック */
    if ((buffer == NULL) || (data == NULL) || (size == 0)) {
        return RINGBUFFER_APIRESULT_INVALID_ARGUMENT;
    }

    /* バッファに空き領域がない */
    if (size > RingBuffer_GetCapacitySize(buffer)) {
        return RINGBUFFER_APIRESULT_EXCEED_MAX_CAPACITY;
    }

    /* リングバッファを巡回するケース: バッファ末尾までまず書き込み */
    if (buffer->write_pos + size >= buffer->buffer_size) {
        uint8_t *wp = buffer->data + buffer->write_pos;
        const size_t data_head_size = buffer->buffer_size - buffer->write_pos;
        memcpy(wp, data, data_head_size);
        data = (const void *)((uint8_t *)data + data_head_size);
        size -= data_head_size;
        buffer->write_pos = 0;
    }

    /* 剰余領域への書き込み */
    if (buffer->write_pos < buffer->max_required_size) {
        uint8_t *wp = buffer->data + buffer->buffer_size + buffer->write_pos;
        const size_t copy_size = RINGBUFFER_MIN(size, buffer->max_required_size - buffer->write_pos);
        memcpy(wp, data, copy_size);
    }

    /* リングバッファへの書き込み */
    memcpy(buffer->data + buffer->write_pos, data, size);
    buffer->write_pos += size; /* 巡回するケースでインデックスの剰余処理済 */

    return RINGBUFFER_APIRESULT_OK;
}

/* データ見るだけ（バッファの状態は更新されない） 注意）バッファが一周する前に使用しないと上書きされる */
RingBufferApiResult RingBuffer_Peek(
        struct RingBuffer *buffer, void **pdata, size_t required_size)
{
    /* 引数チェック */
    if ((buffer == NULL) || (pdata == NULL) || (required_size == 0)) {
        return RINGBUFFER_APIRESULT_INVALID_ARGUMENT;
    }

    /* 最大要求サイズを超えている */
    if (required_size > buffer->max_required_size) {
        return RINGBUFFER_APIRESULT_EXCEED_MAX_REQUIRED;
    }

    /* 残りデータサイズを超えている */
    if (required_size > RingBuffer_GetRemainSize(buffer)) {
        return RINGBUFFER_APIRESULT_EXCEED_MAX_REMAIN;
    }

    /* データの参照取得 */
    (*pdata) = (void *)(buffer->data + buffer->read_pos);

    return RINGBUFFER_APIRESULT_OK;
}

/* データ取得 注意）バッファが一周する前に使用しないと上書きされる */
RingBufferApiResult RingBuffer_Get(
        struct RingBuffer *buffer, void **pdata, size_t required_size)
{
    RingBufferApiResult ret;

    /* 読み出し */
    if ((ret = RingBuffer_Peek(buffer, pdata, required_size)) != RINGBUFFER_APIRESULT_OK) {
        return ret;
    }

    /* バッファ参照位置更新 */
    buffer->read_pos = (buffer->read_pos + (uint32_t)required_size) % buffer->buffer_size;

    return RINGBUFFER_APIRESULT_OK;
}

