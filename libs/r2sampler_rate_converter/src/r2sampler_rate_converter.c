#include <r2sampler.h>

#include <stdlib.h>
#include <assert.h>

#include "ring_buffer.h"
#include "r2sampler_utility.h"

/* メモリアラインメント */
#define R2SAMPLERRATECONVERTER_ALIGNMENT 16
/* 最大値の選択 */
#define R2SAMPLERRATECONVERTER_MAX(a, b) (((a) > (b)) ? (a) : (b))
/* 最小値の選択 */
#define R2SAMPLERRATECONVERTER_MIN(a, b) (((a) < (b)) ? (a) : (b))
/* nの倍数に切り上げ */
#define R2SAMPLERRATECONVERTER_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* レート変換器ハンドル */
struct R2samplerRateConverter {
    uint32_t max_num_input_samples;
    uint32_t up_rate;
    uint32_t down_rate;
    struct RingBuffer *output_buffer;
    float *interp_buffer;
    uint32_t num_interp_buffer_samples;
    R2samplerFilterType filter_type;
    uint32_t filter_order;
    float *filter_coef;
    uint32_t interp_offset;
    uint8_t alloc_by_own;
    void *work;
};

/* レート変換器作成に必要なワークサイズ計算 */
int32_t R2samplerRateConverter_CalculateWorkSize(const struct R2samplerRateConverterConfig *config)
{
    int32_t work_size;
    uint32_t tmp_up_rate;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_num_input_samples == 0)
            || (config->input_rate == 0) || (config->output_rate == 0)) {
        return -1;
    }
    /* フィルタ次数は奇数を要求 */
    if ((config->filter_order % 2) == 0) {
        return -1;
    }
    /* フィルタを適用しない場合は次数は1を要求 */
    if ((config->filter_type == R2SAMPLER_FILTERTYPE_NONE) && (config->filter_order != 1)) {
        return -1;
    }

    /* ワークサイズ計算 */
    work_size = sizeof(struct R2samplerRateConverter) + R2SAMPLERRATECONVERTER_ALIGNMENT;

    /* バッファワークサイズ計算 */
    {
        int32_t tmp_work_size;
        uint32_t gcd, tmp_down_rate, buffer_num_samples;
        struct RingBufferConfig buffer_config;

        /* バッファに必要なサンプル数（=正規化した入出力レート）を計算 */
        gcd = R2sampler_GCD(config->input_rate, config->output_rate);
        assert(((config->input_rate % gcd) == 0) && ((config->output_rate % gcd) == 0));
        tmp_up_rate = config->output_rate / gcd;
        tmp_down_rate = config->input_rate / gcd;

        /* TODO: 出力レートが低すぎる場合は今のところ作成不可。入力サンプル数を多くして対応してもらう */
        if ((tmp_up_rate * config->max_num_input_samples) < tmp_down_rate) {
            return -1;
        }

        /* ワークサイズ計算*/
        /* バッファサンプル数: 最大入力数+間引き時に残りうるサンプル数にフィルタサイズ分 */
        buffer_num_samples = config->max_num_input_samples * tmp_up_rate + (tmp_down_rate - 1) + config->filter_order;
        buffer_config.max_size = sizeof(float) * buffer_num_samples;
        buffer_config.max_required_size = sizeof(float) * R2SAMPLERRATECONVERTER_MAX(tmp_down_rate, config->filter_order);
        if ((tmp_work_size = RingBuffer_CalculateWorkSize(&buffer_config)) < 0) {
            return -1;
        }
        work_size += tmp_work_size;
    }

    /* フィルタ係数サイズ計算 */
    work_size += sizeof(float) * config->filter_order + R2SAMPLERRATECONVERTER_ALIGNMENT;
    /* 補間データバッファサイズ計算 */
    work_size += sizeof(float) * config->max_num_input_samples * tmp_up_rate + R2SAMPLERRATECONVERTER_ALIGNMENT;

    return work_size;
}

/* レート変換器作成 */
struct R2samplerRateConverter *R2samplerRateConverter_Create(
        const struct R2samplerRateConverterConfig *config, void *work, int32_t work_size)
{
    struct R2samplerRateConverter *converter;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t *work_ptr;
    uint32_t tmp_up_rate;

    /* ワーク領域時前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = R2samplerRateConverter_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((size_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < R2samplerRateConverter_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_num_input_samples == 0)
            || (config->input_rate == 0) || (config->output_rate == 0)) {
        return NULL;
    }
    /* フィルタ次数は奇数を要求 */
    if ((config->filter_order % 2) == 0) {
        return NULL;
    }
    /* フィルタを適用しない場合は次数は1を要求 */
    if ((config->filter_type == R2SAMPLER_FILTERTYPE_NONE) && (config->filter_order != 1)) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t *)work;

    /* ハンドル領域確保 */
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter = (struct R2samplerRateConverter *)work_ptr;
    work_ptr += sizeof(struct R2samplerRateConverter);

    /* メンバ設定 */
    converter->max_num_input_samples = config->max_num_input_samples;
    converter->filter_type = config->filter_type;
    converter->filter_order = config->filter_order;
    converter->alloc_by_own = tmp_alloc_by_own;
    converter->work = work;

    /* バッファ作成 */
    {
        int32_t tmp_work_size;
        uint32_t gcd, tmp_down_rate, buffer_num_samples;
        struct RingBufferConfig buffer_config;

        /* バッファに必要なサンプル数（=正規化した入力レート）を計算 */
        gcd = R2sampler_GCD(config->input_rate, config->output_rate);
        assert(((config->input_rate % gcd) == 0) && ((config->output_rate % gcd) == 0));
        tmp_up_rate = config->output_rate / gcd;
        tmp_down_rate = config->input_rate / gcd;

        /* TODO: 出力レートが低すぎる場合は今のところ作成不可。入力サンプル数を多くして対応してもらう */
        if ((tmp_up_rate * config->max_num_input_samples) < tmp_down_rate) {
            return NULL;
        }

        /* バッファ作成 */
        /* バッファサンプル数: 最大入力数+間引き時に残りうるサンプル数にフィルタサイズ分 */
        buffer_num_samples = config->max_num_input_samples * tmp_up_rate + (tmp_down_rate - 1) + config->filter_order;
        buffer_config.max_size = sizeof(float) * buffer_num_samples;
        buffer_config.max_required_size = sizeof(float) * R2SAMPLERRATECONVERTER_MAX(tmp_down_rate, config->filter_order);
        if ((tmp_work_size = RingBuffer_CalculateWorkSize(&buffer_config)) < 0) {
            return NULL;
        }
        converter->output_buffer = RingBuffer_Create(&buffer_config, work_ptr, tmp_work_size);
        assert(converter->output_buffer != NULL);
        work_ptr += tmp_work_size;

        /* 正規化した入出力レートを記録 */
        converter->up_rate = tmp_up_rate;
        converter->down_rate = tmp_down_rate;
    }

    /* フィルタ係数の領域確保 */
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter->filter_coef = (float *)work_ptr;
    work_ptr += sizeof(float) * config->filter_order;

    /* 補間データバッファの領域確保 */
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter->interp_buffer = (float *)work_ptr;
    work_ptr += sizeof(float) * config->max_num_input_samples * tmp_up_rate;
    converter->num_interp_buffer_samples = config->max_num_input_samples * tmp_up_rate;

    /* バッファオーバーランチェック */
    assert((work_ptr - (uint8_t *)work) <= work_size);

    /* フィルタ設計 */
    switch (converter->filter_type) {
    case R2SAMPLER_FILTERTYPE_NONE:
        /* インパルス応答の畳込みとする */
        assert(converter->filter_order == 1);
        converter->filter_coef[0] = 1.0f;
        break;
    case R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW:
    case R2SAMPLER_FILTERTYPE_LPF_BLACKMANWINDOW:
    case R2SAMPLER_FILTERTYPE_LPF_NUTTALLWINDOW:
    case R2SAMPLER_FILTERTYPE_LPF_BLACKMANNUTTALLWINDOW:
        {
            uint32_t i;
            R2samplerLPFWindowType window_type = R2SAMPLERLPF_WINDOW_TYPE_INVALID;
            /* 阻止域: エイリアシング防止のため狭い方に設定 */
            const float cutoff = 0.5f / R2SAMPLERRATECONVERTER_MAX(converter->up_rate, converter->down_rate);
            /* フィルタ設計 */
            switch (converter->filter_type) {
            case R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW:
                window_type = R2SAMPLERLPF_WINDOW_TYPE_HANN;
                break;
            case R2SAMPLER_FILTERTYPE_LPF_BLACKMANWINDOW:
                window_type = R2SAMPLERLPF_WINDOW_TYPE_BLACKMAN;
                break;
            case R2SAMPLER_FILTERTYPE_LPF_NUTTALLWINDOW:
                window_type = R2SAMPLERLPF_WINDOW_TYPE_NUTTALL;
                break;
            case R2SAMPLER_FILTERTYPE_LPF_BLACKMANNUTTALLWINDOW:
                window_type = R2SAMPLERLPF_WINDOW_TYPE_BLACKMANNUTTALL;
                break;
            default:
                assert(0);
            }
            assert(window_type != R2SAMPLERLPF_WINDOW_TYPE_INVALID);
            R2sampler_CreateLPFByWindowFunction(cutoff,
                    window_type, converter->filter_coef, converter->filter_order);
            /* 利得調整 */
            for (i = 0; i < converter->filter_order; i++) {
                converter->filter_coef[i] *= converter->up_rate;
            }
        }
        break;
    default:
        assert(0);
    }

    /* 作成直後にレート変換を行えるように開始を指示 */
    (void)R2samplerRateConverter_Start(converter);

    return converter;
}

/* レート変換器破棄 */
void R2samplerRateConverter_Destroy(struct R2samplerRateConverter *converter)
{
    if (converter != NULL) {
        /* 先にバッファを破棄しておく */
        RingBuffer_Destroy(converter->output_buffer);
        if (converter->alloc_by_own == 1) {
            free(converter->work);
        }
    }
}

/* レート変換開始（内部バッファリセット） */
R2samplerRateConverterApiResult R2samplerRateConverter_Start(struct R2samplerRateConverter *converter)
{
    uint32_t i;

    /* 引数チェック */
    if (converter == NULL) {
        return R2SAMPLERRATECONVERTER_APIRESULT_INVALID_ARGUMENT;
    }

    /* リングバッファクリア */
    RingBuffer_Clear(converter->output_buffer);

    /* 補間バッファをゼロ埋め */
    for (i = 0; i < converter->num_interp_buffer_samples; i++) {
        converter->interp_buffer[i] = 0.0f;
    }

    /* フィルタ係数-1分の遅延を挿入（次に入るサンプルが丁度末尾に入るように） */
    i = 0;
    while (i < (converter->filter_order - 1)) {
        uint32_t num_put_samples = R2SAMPLERRATECONVERTER_MIN(
                converter->num_interp_buffer_samples, (converter->filter_order - 1) - i);
        RingBuffer_Put(converter->output_buffer, converter->interp_buffer, sizeof(float) * num_put_samples);
        i += num_put_samples;
    }

    /* ゼロ値挿入したデータの非ゼロ値のオフセットをリセット */
    converter->interp_offset = (converter->filter_order - 1) % converter->up_rate;

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}

/* 内部でバッファリングしているサンプル数を取得 */
static uint32_t R2samplerRateConverter_GetNumBufferedSamples(const struct R2samplerRateConverter *converter)
{
    uint32_t num_buffered_samples;

    /* 引数チェック */
    assert(converter != NULL);

    num_buffered_samples = (uint32_t)RingBuffer_GetRemainSize(converter->output_buffer) / sizeof(float);

    /* 遅延分を削除 */
    assert(num_buffered_samples >= (converter->filter_order - 1));
    return num_buffered_samples - (converter->filter_order - 1);
}

/* 入力サンプル数に対して得られる出力サンプル数を取得 */
static uint32_t R2samplerRateConverter_GetNumOutputSamples(
        const struct R2samplerRateConverter *converter, uint32_t num_input_samples)
{
    uint32_t nsmpls;

    /* 引数チェック */
    assert(converter != NULL);

    /* リングバッファ内と入力の補間後サンプル数を合算 */
    nsmpls = R2samplerRateConverter_GetNumBufferedSamples(converter);
    nsmpls += converter->up_rate * num_input_samples;

    /* 間引いた数だけ出力可能 */
    return nsmpls / converter->down_rate;
}

/* レート変換 */
R2samplerRateConverterApiResult R2samplerRateConverter_Process(
        struct R2samplerRateConverter *converter,
        const float *input, uint32_t num_input_samples,
        float *output_buffer, uint32_t num_buffer_samples, uint32_t *num_output_samples)
{
    uint32_t smpl, tmp_num_output_samples;
    RingBufferApiResult rbf_ret;

    /* 引数チェック */
    if ((converter == NULL) || (input == NULL)
            || (output_buffer == NULL) || (num_output_samples == NULL)) {
        return R2SAMPLERRATECONVERTER_APIRESULT_INVALID_ARGUMENT;
    }

    /* 入力サンプル数が多すぎる */
    if (num_input_samples > converter->max_num_input_samples) {
        return R2SAMPLERRATECONVERTER_APIRESULT_TOOMANY_NUM_INPUTS;
    }

    /* 出力サンプル数の計算 */
    tmp_num_output_samples = R2samplerRateConverter_GetNumOutputSamples(converter, num_input_samples);

    /* バッファサイズ不足 */
    if (tmp_num_output_samples > num_buffer_samples) {
        return R2SAMPLERRATECONVERTER_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* サンプル値+ゼロ値挿入 */
    for (smpl = 0; smpl < num_input_samples * converter->up_rate; smpl++) {
        converter->interp_buffer[smpl] = 0.0f;
    }
    for (smpl = 0; smpl < num_input_samples; smpl++) {
        converter->interp_buffer[smpl * converter->up_rate] = input[smpl];
    }

    /* ゼロ値挿入したデータをディレイバッファに入力 */
    rbf_ret = RingBuffer_Put(converter->output_buffer,
            converter->interp_buffer, sizeof(float) * converter->up_rate * num_input_samples);
    assert(rbf_ret == RINGBUFFER_APIRESULT_OK);

    /* 間引きしつつフィルタリング */
    if (converter->up_rate > 1) {
        /* ゼロ値挿入分をスキップした処理 */
        /* ゼロ値挿入したデータの先頭位置の更新量: up_rate - down_rate */
        const uint32_t interp_delta = converter->down_rate * (converter->up_rate - 1);
        for (smpl = 0; smpl < tmp_num_output_samples; smpl++) {
            uint32_t i;
            float *pdecim;
            /* ディレイバッファから取得（同時にdown_rateだけ進めて間引く） */
            rbf_ret = RingBuffer_Get(converter->output_buffer, (void **)&pdecim, sizeof(float) * converter->down_rate);
            assert(rbf_ret == RINGBUFFER_APIRESULT_OK);
            /* up_rate間隔でデータが並んでいる以外は全て0なので積和演算をスキップ可 */
            output_buffer[smpl] = 0.0f;
            for (i = converter->interp_offset; i < converter->filter_order; i += converter->up_rate) {
                output_buffer[smpl] += pdecim[i] * converter->filter_coef[i];
            }
            /* ゼロ値挿入したデータの非ゼロ値のオフセット更新 */
            converter->interp_offset = (converter->interp_offset + interp_delta) % converter->up_rate;
        }
    } else {
        /* 通常のFIRフィルタによる畳み込み */
        const uint32_t half_order = converter->filter_order / 2;
        for (smpl = 0; smpl < tmp_num_output_samples; smpl++) {
            uint32_t i;
            float *pdecim;
            /* ディレイバッファから取得（同時にdown_rateだけ進めて間引く） */
            rbf_ret = RingBuffer_Get(converter->output_buffer, (void **)&pdecim, sizeof(float) * converter->down_rate);
            assert(rbf_ret == RINGBUFFER_APIRESULT_OK);
            /* フィルタ適用: 係数は奇数かつ偶対象であることを使用 */
            output_buffer[smpl] = pdecim[half_order] * converter->filter_coef[half_order];
            for (i = 0; i < half_order; i++) {
                output_buffer[smpl] += (pdecim[i] + pdecim[converter->filter_order - i - 1]) * converter->filter_coef[i];
            }
        }
    }

    /* 出力サンプル数をセット */
    (*num_output_samples) = tmp_num_output_samples;

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}
