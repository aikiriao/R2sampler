#include "r2sampler_lpf.h"

#include <math.h>
#include <assert.h>
#include <stddef.h>

/* 円周率 */
#define R2SAMPLER_PI 3.14159265358979323846

/* sinc関数 */
static float sinc(float x)
{
    return (fabs(x) > 1.0e-8) ? (sinf(x) / x) : 1.0f;
}

/* ハン窓 0 <= x <= 1 */
static float hanning_window(float x)
{
    return 0.5f - 0.5f * cosf(2.0f * (float)R2SAMPLER_PI * x);
}

/* 窓関数によりLPF設計 */
void R2samplerLPF_CreateLPFByWindowFunction(
        float cutoff, R2samplerLPFWindowType window_type,
        float *filter_coef, uint32_t filter_order)
{
    uint32_t i;
    const float half = (filter_order - 1.0f) / 2.0f;

    /* 引数チェック */
    assert(filter_coef != NULL);
    assert(filter_order > 0);
    assert((cutoff >= 0.0f) && (cutoff < 1.0f));
    /* フィルタ次数は奇数を要求（直線位相特性のため） */
    assert(filter_order % 2 == 1);

    /* sinc関数により理想LPFの係数を取得 */
    for (i = 0; i < filter_order; i++) {
        const float x = (float)i - half;
        filter_coef[i] = 2.0f * cutoff * sinc(2.0f * (float)R2SAMPLER_PI * cutoff * x);
    }

    /* 窓関数適用 */
    switch (window_type) {
    case R2SAMPLERLPF_WINDOW_TYPE_RECTANGULAR:
        /* 矩形窓: 何もせず打ち切り */
        break;
    case R2SAMPLERLPF_WINDOW_TYPE_HANN:
        /* ハン窓 */
        for (i = 0; i < filter_order; i++) {
            filter_coef[i] *= hanning_window(i / (filter_order - 1.0f));
        }
        break;
    default:
        assert(0);
    }

}
