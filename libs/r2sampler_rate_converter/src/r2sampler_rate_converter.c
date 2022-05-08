#include <r2sampler.h>

#include <stdlib.h>
#include <assert.h>

#include "ring_buffer.h"
#include "r2sampler_utility.h"

/* メモリアラインメント */
#define R2SAMPLERRATECONVERTER_ALIGNMENT 16
/* 最大値の選択 */
#define R2SAMPLERRATECONVERTER_MAX(a, b) (((a) > (b)) ? (a) : (b))
/* nの倍数に切り上げ */
#define R2SAMPLERRATECONVERTER_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* FIRフィルタ */
struct R2samplerFIRFilter {
    uint32_t order;
    float *coef;
    float *history;
    int32_t pos;
};

/* レート変換器ハンドル */
struct R2samplerRateConverter {
    uint32_t max_num_input_samples;
    uint32_t up_rate;
    uint32_t down_rate;
    struct RingBuffer *output_buffer;
    float *interp_buffer;
    R2samplerFilterType filter_type;
    struct R2samplerFIRFilter filter;
    uint8_t alloc_by_own;
    void *work;
};

/* 素朴な直線畳み込み(in-place) */
static void R2samplerFIRFilter_Convolve(
        struct R2samplerFIRFilter *filter, float *buffer, uint32_t num_samples)
{
	uint32_t smpl, i, buf_pos;

    assert((filter != NULL) && (buffer != NULL));

    for (smpl = 0; smpl < num_samples; smpl++) {
        /* バッファに入力 */
        filter->history[filter->pos] = buffer[smpl];

		/* 1サンプル当たりの出力計算 */
		buffer[smpl] = 0.0f;
		buf_pos = (uint32_t)filter->pos;
		for (i = 0; i < filter->order; i++) {
			buffer[smpl] += filter->coef[i] * filter->history[buf_pos];
			buf_pos++;
			if (buf_pos >= filter->order) {
				buf_pos = 0;
			}
		}

		/* バッファ参照位置更新 */
		filter->pos--;
		if (filter->pos < 0) {
			filter->pos = (int32_t)(filter->order - 1);
		}
    }
}

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

    /* フィルタ次数は0か奇数を要求 */
    if ((config->filter_order != 0) && ((config->filter_order % 2) == 0)) {
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
        /* バッファサンプル数: 最大入力数+間引き時に残りうるサンプル数 */
        buffer_num_samples = config->max_num_input_samples * tmp_up_rate + (tmp_down_rate - 1);
        buffer_config.max_size = sizeof(float) * buffer_num_samples;
        buffer_config.max_required_size = sizeof(float) * tmp_down_rate;
        if ((tmp_work_size = RingBuffer_CalculateWorkSize(&buffer_config)) < 0) {
            return -1;
        }
        work_size += tmp_work_size;
    }

    /* フィルタワークサイズ計算（係数+入力履歴） */
    work_size += 2 * (sizeof(float) * config->filter_order + R2SAMPLERRATECONVERTER_ALIGNMENT);
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

    /* フィルタ次数は0か奇数を要求 */
    if ((config->filter_order != 0) && ((config->filter_order % 2) == 0)) {
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
        /* バッファサンプル数: 最大入力数+間引き時に残りうるサンプル数 */
        buffer_num_samples = config->max_num_input_samples * tmp_up_rate + (tmp_down_rate - 1);
        buffer_config.max_size = sizeof(float) * buffer_num_samples;
        buffer_config.max_required_size = sizeof(float) * tmp_down_rate;
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

    /* フィルタの領域確保 */
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter->filter.coef = (float *)work_ptr;
    work_ptr += sizeof(float) * config->filter_order;
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter->filter.history = (float *)work_ptr;
    work_ptr += sizeof(float) * config->filter_order;
    converter->filter.order = config->filter_order;

    /* 補間データバッファの領域確保 */
    work_ptr = (uint8_t *)R2SAMPLERRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERRATECONVERTER_ALIGNMENT);
    converter->interp_buffer = (float *)work_ptr;
    work_ptr += sizeof(float) * config->max_num_input_samples * tmp_up_rate;

    /* バッファオーバーランチェック */
    assert((work_ptr - (uint8_t *)work) <= work_size);

    /* 必要であればフィルタ設計 */
    switch (converter->filter_type) {
    case R2SAMPLER_FILTERTYPE_NONE:
    case R2SAMPLER_FILTERTYPE_0ORDER_HOLD:
        break;
    case R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW:
        {
            uint32_t i;
            /* 阻止域の厳しい方に設定 */
            const float cutoff = 0.5f / R2SAMPLERRATECONVERTER_MAX(converter->up_rate, converter->down_rate);
            R2sampler_CreateLPFByWindowFunction(cutoff,
                    R2SAMPLERLPF_WINDOW_TYPE_HANN, converter->filter.coef, converter->filter.order);
            /* 利得調整 */
            for (i = 0; i < converter->filter.order; i++) {
                converter->filter.coef[i] *= converter->up_rate;
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
    for (i = 0; i < converter->up_rate * converter->max_num_input_samples; i++) {
        converter->interp_buffer[i] = 0.0f;
    }

    /* フィルタ履歴をゼロ埋め */
    for (i = 0; i < converter->filter.order; i++) {
        converter->filter.history[i] = 0.0f;
    }
    converter->filter.pos = 0;

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}

/* 入力サンプル数に対して得られる出力サンプル数を取得 */
static uint32_t R2samplerRateConverter_GetNumOutputSamples(
        const struct R2samplerRateConverter *converter, uint32_t num_input_samples)
{
    uint32_t nsmpls;

    /* 引数チェック */
    assert(converter != NULL);

    /* リングバッファ内と入力の補間後サンプル数を合算 */
    nsmpls = (uint32_t)RingBuffer_GetRemainSize(converter->output_buffer) / sizeof(float);
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

    /* フィルタ適用 */
    switch (converter->filter_type) {
    case R2SAMPLER_FILTERTYPE_NONE:
        /* 何もしない */
        break;
    case R2SAMPLER_FILTERTYPE_0ORDER_HOLD:
        /* 0次ホールド */
        {
            uint32_t j;
            for (smpl = 0; smpl < num_input_samples; smpl++) {
                for (j = 1; j < converter->up_rate; j++) {
                    converter->interp_buffer[j + smpl * converter->up_rate] = input[smpl];
                }
            }
        }
        break;
    case R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW:
        /* LPF */
        R2samplerFIRFilter_Convolve(&converter->filter,
                converter->interp_buffer, num_input_samples * converter->up_rate);
        break;
    default:
        assert(0);
    }

    /* フィルタ済みのデータを挿入 */
    rbf_ret = RingBuffer_Put(converter->output_buffer,
            converter->interp_buffer, sizeof(float) * converter->up_rate * num_input_samples);
    assert(rbf_ret == RINGBUFFER_APIRESULT_OK);

    /* 間引き結果を取得 */
    for (smpl = 0; smpl < tmp_num_output_samples; smpl++) {
        void *pdecim;
        rbf_ret = RingBuffer_Get(converter->output_buffer, &pdecim, sizeof(float) * converter->down_rate);
        assert(rbf_ret == RINGBUFFER_APIRESULT_OK);
        output_buffer[smpl] = ((float *)pdecim)[0];
    }

    /* 出力サンプル数をセット */
    (*num_output_samples) = tmp_num_output_samples;

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}
