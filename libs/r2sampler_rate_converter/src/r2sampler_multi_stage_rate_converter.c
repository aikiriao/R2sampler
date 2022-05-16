#include <r2sampler.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ring_buffer.h"
#include "r2sampler_rate_converter.h"
#include "r2sampler_utility.h"

/* メモリアラインメント */
#define R2SAMPLERMSRATECONVERTER_ALIGNMENT 16
/* nの倍数に切り上げ */
#define R2SAMPLERMSRATECONVERTER_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* マルチステージレート変換器ハンドル */
struct R2samplerMultiStageRateConverter {
    struct R2samplerRateConverter **resampler;
    uint32_t up_rate;
    uint32_t down_rate;
    uint32_t max_num_stages;
    uint32_t num_stages;
    uint32_t max_num_input_samples;
    float *process_buffer[2];
    uint32_t max_num_buffer_samples;
    uint8_t alloc_by_own;
    void *work;
};

/* 各ステージでのアップレート・ダウンレート設定 */
struct R2samplerMultiStageUpDownRateConfig {
    uint32_t up_rate;
    uint32_t down_rate;
};

/* 各ステージでのアップレート・ダウンレートを設定 */
static void R2samplerMultiStageRateConverter_SetUpDownRateConfig(
    uint32_t up_rate, uint32_t down_rate,
    struct R2samplerMultiStageUpDownRateConfig *config, uint32_t max_num_stages, uint32_t *num_stages)
{
    uint32_t stage, up_inx;
    uint32_t up_factors[R2SAMPLER_MAX_NUM_STAGES], down_factors[R2SAMPLER_MAX_NUM_STAGES];
    uint32_t num_up_stages, num_down_stages;

    assert((config != NULL) && (num_stages != NULL));
    assert(max_num_stages <= R2SAMPLER_MAX_NUM_STAGES);

    /* 入力レート・出力レートを素因数分解 */
    R2sampler_Factorize(up_rate, up_factors, R2SAMPLER_MAX_NUM_STAGES, &num_up_stages);
    R2sampler_Factorize(down_rate, down_factors, R2SAMPLER_MAX_NUM_STAGES, &num_down_stages);

    /* アップサンプル時に同時に可能な限りダウンサンプリングするように
    * （ダウンサンプリングを入れつつもレートが上がりつづけるように）設定 */
    up_inx = 0;
    for (stage = 0; stage < num_down_stages; stage++) {
        uint32_t up = 1;
        uint32_t down = down_factors[stage];
        /* ダウンレートを追い越すまでアップレートを掛け合わせる */
        while ((up <= down) && (up_inx < num_up_stages)) {
            up *= up_factors[up_inx++];
        }
        config[stage].up_rate = up;
        config[stage].down_rate = down;
    }

    /* ダウンレートと比べ2（最小素数）倍より大きければアップレートの因数を後ろに移動
    * （大アップレートが原因で作られる、狭帯域フィルタによる音質劣化回避） */
    {
        const uint32_t original_num_stages = stage;
        uint32_t i, j, tmp_num_up_factors, tmp_up_factors[R2SAMPLER_MAX_NUM_STAGES];
REDUCE_BIGUPRATE:
        for (i = 0; i < original_num_stages; i++) {
            if (config[i].down_rate == 1) {
                break;
            }
            if (config[i].up_rate >= (2 * config[i].down_rate)) {
                uint32_t moved_up_rate;
                R2sampler_Factorize(config[i].up_rate,
                        tmp_up_factors, R2SAMPLER_MAX_NUM_STAGES, &tmp_num_up_factors);
                /* 素因数ならばスキップ */
                if (tmp_num_up_factors == 1) {
                    continue;
                }
                /* アップレート>ダウンレートを満たす範囲内で因数を探す */
                moved_up_rate = 1;
                for (j = 0; j < tmp_num_up_factors; j++) {
                    if ((config[i].up_rate / tmp_up_factors[j]) > config[i].down_rate) {
                        moved_up_rate = tmp_up_factors[j];
                    }
                }
                /* 因数がなければスキップ */
                if (moved_up_rate == 1) {
                    continue;
                }
                config[i].up_rate /= moved_up_rate;
                /* アップレートを後ろに移動 */
                for (j = i + 1; j < original_num_stages; j++) {
                    if (config[j].up_rate < config[j].down_rate) {
                        config[j].up_rate *= moved_up_rate;
                        break;
                    }
                }
                /* ステージ追加 */
                if (j == original_num_stages) {
                    config[stage].up_rate = moved_up_rate;
                    config[stage].down_rate = 1;
                    stage++;
                }
                /* 最初から検索 */
                goto REDUCE_BIGUPRATE;
            }
        }
    }

    /* 残ったアップレートを配置 */
    for (; up_inx < num_up_stages; up_inx++) {
        config[stage].up_rate = up_factors[up_inx];
        config[stage].down_rate = 1;
        stage++;
    }

    /* TODO: まだ最適ではない。全ステージでの(アップレート - ダウンレート)の和を最小化するべき
    * 例) 44100Hz -> 11000Hz だと(Up,Down)が(5,3), (11,3), (2,7), (1,7)になるが、
    * (5,3), (11,7), (2,3), (1,7)の方がより良い
    * 網羅的に(アップレート - ダウンレート)の和を最小化する組み合わせを探す？ */

    /* 結果をセット */
    assert(stage < max_num_stages);
    (*num_stages) = stage;
}

/* レート変換器作成に必要なワークサイズ計算 */
int32_t R2samplerMultiStageRateConverter_CalculateWorkSize(const struct R2samplerMultiStageRateConverterConfig *config)
{
    int32_t work_size, tmp_work_size;
    uint32_t i, gcd, tmp_up_rate, tmp_down_rate, tmp_max_num_input_samples;
    struct R2samplerRateConverterConfig tmp_config;

    uint32_t num_stages;
    struct R2samplerMultiStageUpDownRateConfig udconfig[R2SAMPLER_MAX_NUM_STAGES];

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if ((config->max_num_stages == 0) || (config->single.max_num_input_samples == 0)
            || (config->single.input_rate == 0) || (config->single.output_rate == 0)
            || (config->max_num_stages > R2SAMPLER_MAX_NUM_STAGES)) {
        return -1;
    }

    /* ワークサイズ計算 */
    work_size = sizeof(struct R2samplerMultiStageRateConverter) + R2SAMPLERMSRATECONVERTER_ALIGNMENT;

    /* 互いに素な入出力レートを計算 */
    gcd = R2sampler_GCD(config->single.input_rate, config->single.output_rate);
    tmp_up_rate = config->single.output_rate / gcd;
    tmp_down_rate = config->single.input_rate / gcd;

    /* 補間データバッファx2サイズ計算 */
    work_size += 2 * (sizeof(float) * config->single.max_num_input_samples * tmp_up_rate + R2SAMPLERMSRATECONVERTER_ALIGNMENT);

    /* 各ステージでのアップレート・ダウンレートを設定 */
    R2samplerMultiStageRateConverter_SetUpDownRateConfig(tmp_up_rate, tmp_down_rate, udconfig, R2SAMPLER_MAX_NUM_STAGES, &num_stages);

    /* ハンドルのポインタ領域を計算 */
    work_size += sizeof(struct R2samplerRateConverter*) * num_stages + R2SAMPLERMSRATECONVERTER_ALIGNMENT;

    /* レート変換器のサイズを計算 */
    tmp_max_num_input_samples = config->single.max_num_input_samples;
    for (i = 0; i < num_stages; i++) {
        tmp_config.max_num_input_samples = tmp_max_num_input_samples;
        tmp_config.input_rate = udconfig[i].down_rate;
        tmp_config.output_rate = udconfig[i].up_rate;
        tmp_config.filter_type = config->single.filter_type; /* 各ステージで変えるのもあり */
        tmp_config.filter_order = config->single.filter_order; /* 各ステージで変えるのもあり */
        if ((tmp_work_size = R2samplerRateConverter_CalculateWorkSize(&tmp_config)) < 0) {
            return -1;
        }
        work_size += tmp_work_size;
        /* 次のステージで必要になるサンプル数 */
        tmp_max_num_input_samples
            = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(tmp_max_num_input_samples, udconfig[i].down_rate, udconfig[i].up_rate);
    }

    return work_size;
}

/* レート変換器作成 */
struct R2samplerMultiStageRateConverter* R2samplerMultiStageRateConverter_Create(
    const struct R2samplerMultiStageRateConverterConfig* config, void* work, int32_t work_size)
{
    struct R2samplerMultiStageRateConverter* converter;
    uint8_t tmp_alloc_by_own = 0;
    uint8_t* work_ptr;
    uint32_t i, gcd, tmp_up_rate, tmp_down_rate;
    uint32_t num_stages;
    struct R2samplerMultiStageUpDownRateConfig udconfig[R2SAMPLER_MAX_NUM_STAGES];

    /* ワーク領域時前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = R2samplerMultiStageRateConverter_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((size_t)work_size);
        tmp_alloc_by_own = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
        || (work_size < R2samplerMultiStageRateConverter_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if ((config->max_num_stages == 0) || (config->single.max_num_input_samples == 0)
        || (config->single.input_rate == 0) || (config->single.output_rate == 0)
        || (config->max_num_stages > R2SAMPLER_MAX_NUM_STAGES)) {
        return NULL;
    }

    /* ワーク領域先頭ポインタ取得 */
    work_ptr = (uint8_t*)work;

    /* ハンドル領域確保 */
    work_ptr = (uint8_t*)R2SAMPLERMSRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERMSRATECONVERTER_ALIGNMENT);
    converter = (struct R2samplerMultiStageRateConverter*)work_ptr;
    work_ptr += sizeof(struct R2samplerMultiStageRateConverter);

    /* メンバ設定 */
    converter->max_num_input_samples = config->single.max_num_input_samples;
    converter->max_num_stages = config->max_num_stages;
    converter->alloc_by_own = tmp_alloc_by_own;
    converter->work = work;

    /* 互いに素な入出力レートを計算 */
    gcd = R2sampler_GCD(config->single.input_rate, config->single.output_rate);
    tmp_up_rate = config->single.output_rate / gcd;
    tmp_down_rate = config->single.input_rate / gcd;

    /* 処理データバッファの領域確保 */
    for (i = 0; i < 2; i++) {
        work_ptr = (uint8_t*)R2SAMPLERMSRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERMSRATECONVERTER_ALIGNMENT);
        converter->process_buffer[i] = (float*)work_ptr;
        work_ptr += sizeof(float) * config->single.max_num_input_samples * tmp_up_rate;
    }

    /* 入出力レートを記録 */
    converter->up_rate = tmp_up_rate;
    converter->down_rate = tmp_down_rate;
    converter->max_num_buffer_samples = tmp_up_rate * config->single.max_num_input_samples;

    /* 各ステージでのアップレート・ダウンレートを設定 */
    R2samplerMultiStageRateConverter_SetUpDownRateConfig(tmp_up_rate, tmp_down_rate, udconfig, R2SAMPLER_MAX_NUM_STAGES, &num_stages);

    /* ステージ数を記録 */
    converter->num_stages = num_stages;

    /* レート変換器ハンドルのポインタ領域を確保 */
    work_ptr = (uint8_t*)R2SAMPLERMSRATECONVERTER_ROUNDUP((uintptr_t)work_ptr, R2SAMPLERMSRATECONVERTER_ALIGNMENT);
    converter->resampler = (struct R2samplerRateConverter **)work_ptr;
    work_ptr += sizeof(struct R2samplerRateConverter *) * num_stages;

    {
        int32_t tmp_work_size;
        uint32_t tmp_max_num_input_samples;
        struct R2samplerRateConverterConfig tmp_config;

        /* レート変換器作成 */
        tmp_max_num_input_samples = config->single.max_num_input_samples;
        for (i = 0; i < num_stages; i++) {
            tmp_config.max_num_input_samples = tmp_max_num_input_samples;
            tmp_config.input_rate = udconfig[i].down_rate;
            tmp_config.output_rate = udconfig[i].up_rate;
            tmp_config.filter_type = config->single.filter_type; /* 各ステージで変えるのもあり */
            tmp_config.filter_order = config->single.filter_order; /* 各ステージで変えるのもあり */
            if ((tmp_work_size = R2samplerRateConverter_CalculateWorkSize(&tmp_config)) < 0) {
                return NULL;
            }
            if ((converter->resampler[i] = R2samplerRateConverter_Create(&tmp_config, work_ptr, tmp_work_size)) == NULL) {
                return NULL;
            }
            work_ptr += tmp_work_size;
            /* 次のステージで必要になるサンプル数 */
            tmp_max_num_input_samples
                = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(tmp_max_num_input_samples, udconfig[i].down_rate, udconfig[i].up_rate);
        }
    }

    /* バッファオーバーランチェック */
    assert((work_ptr - (uint8_t *)work) <= work_size);

    /* 作成直後にレート変換を行えるように開始を指示 */
    (void)R2samplerMultiStageRateConverter_Start(converter);

    return converter;
}

/* レート変換器破棄 */
void R2samplerMultiStageRateConverter_Destroy(struct R2samplerMultiStageRateConverter *converter)
{
    if (converter != NULL) {
        uint32_t i;
        /* レート変換器を破棄 */
        for (i = 0; i < converter->num_stages; i++) {
            R2samplerRateConverter_Destroy(converter->resampler[i]);
        }
        if (converter->alloc_by_own == 1) {
            free(converter->work);
        }
    }
}

/* レート変換開始（内部バッファリセット） */
R2samplerRateConverterApiResult R2samplerMultiStageRateConverter_Start(struct R2samplerMultiStageRateConverter *converter)
{
    uint32_t i;

    /* 引数チェック */
    if (converter == NULL) {
        return R2SAMPLERRATECONVERTER_APIRESULT_INVALID_ARGUMENT;
    }

    /* 処理バッファをゼロ埋め */
    for (i = 0; i < converter->up_rate * converter->max_num_input_samples; i++) {
        converter->process_buffer[0][i] = converter->process_buffer[1][i] = 0.0f;
    }

    /* リサンプラをリセット */
    for (i = 0; i < converter->num_stages; i++) {
        R2samplerRateConverter_Start(converter->resampler[i]);
    }

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}

/* レート変換 */
R2samplerRateConverterApiResult R2samplerMultiStageRateConverter_Process(
        struct R2samplerMultiStageRateConverter *converter,
        const float *input, uint32_t num_input_samples,
        float *output_buffer, uint32_t num_buffer_samples, uint32_t *num_output_samples)
{
    uint32_t i, num_input, num_output;
    float *pinput, *poutput;
    R2samplerRateConverterApiResult ret;
    /* ポインタの入れ替え */
#define SWAP_POINTER(p1, p2) \
    do {\
        float* tmp__;\
        tmp__ = p1; p1 = p2; p2 = tmp__;\
    } while(0);

    /* 引数チェック */
    if ((converter == NULL) || (input == NULL)
            || (output_buffer == NULL) || (num_output_samples == NULL)) {
        return R2SAMPLERRATECONVERTER_APIRESULT_INVALID_ARGUMENT;
    }

    /* 入力サンプル数が多すぎる */
    if (num_input_samples > converter->max_num_input_samples) {
        return R2SAMPLERRATECONVERTER_APIRESULT_TOOMANY_NUM_INPUTS;
    }

    /* 処理ポインタをセット */
    pinput = converter->process_buffer[0];
    poutput = converter->process_buffer[1];
    num_input = num_input_samples;

    /* 入力データをセット */
    memcpy(pinput, input, sizeof(float) * num_input_samples);

    /* リサンプル */
    for (i = 0; i < converter->num_stages; i++) {
        if ((ret = R2samplerRateConverter_Process(converter->resampler[i],
            pinput, num_input, poutput,
            converter->max_num_buffer_samples, &num_output)) != R2SAMPLERRATECONVERTER_APIRESULT_OK) {
            return ret;
        }
        /* 途中で出力がなくなった場合はそこで中断 */
        if (num_output == 0) {
            (*num_output_samples) = 0;
            return R2SAMPLERRATECONVERTER_APIRESULT_OK;
        }
        /* 出力を次の入力に差し替え */
        SWAP_POINTER(pinput, poutput);
        num_input = num_output;
    }

    /* 出力データを取得（最後に入れ替えが入るので入力ポインタが最終結果を指している） */
    assert(num_buffer_samples >= num_output);
    memcpy(output_buffer, pinput, sizeof(float) * num_output);

    /* 出力サンプル数をセット */
    (*num_output_samples) = num_output;

    return R2SAMPLERRATECONVERTER_APIRESULT_OK;
}
