#ifndef R2SAMPLER_H_INCLUDED
#define R2SAMPLER_H_INCLUDED

#include <stdint.h>

/* ライブラリバージョン */
#define R2SAMPLER_VERSION 3

/* 最大のステージ数 */
#define R2SAMPLER_MAX_NUM_STAGES 10

/* 入力に対して必要になる最大のサンプル数計算 */
/* 入力サンプル * (出力レート/入力レート) の切り上げが基本で、内部にバッファリングされるサンプルを考慮しoutput_rateを加算 */
#define R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(max_num_input_samples, input_rate, output_rate)\
    ((((max_num_input_samples) * (output_rate) + (output_rate) + (input_rate) - 1)) / (input_rate))

/* （ローパス）フィルタタイプ */
typedef enum R2samplerFilterType {
    R2SAMPLER_FILTERTYPE_NONE = 0,           /* フィルタを適用しない */
    R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW,     /* Hann窓によるLPF */
    R2SAMPLER_FILTERTYPE_LPF_BLACKMANWINDOW, /* Blackman窓によるLPF */
    R2SAMPLER_FILTERTYPE_LPF_NUTTALLWINDOW,  /* Nuttall窓によるLPF */
    R2SAMPLER_FILTERTYPE_LPF_BLACKMANNUTTALLWINDOW /* Blackman-Nuttall窓によるLPF */
} R2samplerFilterType;

/* レート変換器生成コンフィグ */
struct R2samplerRateConverterConfig {
    uint32_t max_num_input_samples;
    uint32_t input_rate;
    uint32_t output_rate;
    R2samplerFilterType filter_type;
    uint32_t filter_order; /* 奇数 */
};

/* マルチステージレート変換器生成コンフィグ */
struct R2samplerMultiStageRateConverterConfig {
    struct R2samplerRateConverterConfig single;
    uint32_t max_num_stages;
};

/* API結果型 */
typedef enum R2samplerRateConverterApiResult {
    R2SAMPLERRATECONVERTER_APIRESULT_OK = 0,
    R2SAMPLERRATECONVERTER_APIRESULT_INVALID_ARGUMENT,
    R2SAMPLERRATECONVERTER_APIRESULT_TOOMANY_NUM_INPUTS,
    R2SAMPLERRATECONVERTER_APIRESULT_INSUFFICIENT_BUFFER,
    R2SAMPLERRATECONVERTER_APIRESULT_NG
} R2samplerRateConverterApiResult;

/* レート変換器ハンドル */
struct R2samplerRateConverter;

/* マルチステージレート変換器ハンドル */
struct R2samplerMultiStageRateConverter;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* レート変換器作成に必要なワークサイズ計算 */
int32_t R2samplerRateConverter_CalculateWorkSize(const struct R2samplerRateConverterConfig *config);

/* レート変換器作成 */
struct R2samplerRateConverter *R2samplerRateConverter_Create(
        const struct R2samplerRateConverterConfig *config, void *work, int32_t work_size);

/* レート変換器破棄 */
void R2samplerRateConverter_Destroy(struct R2samplerRateConverter *converter);

/* レート変換開始（内部バッファリセット） */
R2samplerRateConverterApiResult R2samplerRateConverter_Start(struct R2samplerRateConverter *converter);

/* レート変換 */
R2samplerRateConverterApiResult R2samplerRateConverter_Process(
        struct R2samplerRateConverter *converter,
        const float *input, uint32_t num_input_samples,
        float *output_buffer, uint32_t num_buffer_samples, uint32_t *num_output_samples);

/* マルチステージレート変換器作成に必要なワークサイズ計算 */
int32_t R2samplerMultiStageRateConverter_CalculateWorkSize(const struct R2samplerMultiStageRateConverterConfig *config);

/* マルチステージレート変換器作成 */
struct R2samplerMultiStageRateConverter *R2samplerMultiStageRateConverter_Create(
        const struct R2samplerMultiStageRateConverterConfig *config, void *work, int32_t work_size);

/* マルチステージレート変換器破棄 */
void R2samplerMultiStageRateConverter_Destroy(struct R2samplerMultiStageRateConverter *converter);

/* マルチステージレート変換開始（内部バッファリセット） */
R2samplerRateConverterApiResult R2samplerMultiStageRateConverter_Start(struct R2samplerMultiStageRateConverter *converter);

/* レート変換 */
R2samplerRateConverterApiResult R2samplerMultiStageRateConverter_Process(
        struct R2samplerMultiStageRateConverter *converter,
        const float *input, uint32_t num_input_samples,
        float *output_buffer, uint32_t num_buffer_samples, uint32_t *num_output_samples);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* R2SAMPLER_H_INCLUDED */
