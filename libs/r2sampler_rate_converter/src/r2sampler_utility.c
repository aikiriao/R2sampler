#include "r2sampler_utility.h"

#include <math.h>
#include <assert.h>
#include <stddef.h>

/* 円周率 */
#define R2SAMPLER_PI 3.14159265358979323846

/* sinc関数 */
static double sinc(double x)
{
    return (fabs(x) > 1.0e-8) ? (sin(x) / x) : 1.0;
}

/* ハン窓 0 <= x <= 1 */
static double hanning_window(double x)
{
    return 0.5 - 0.5 * cos(2.0 * R2SAMPLER_PI * x);
}

/* Blackman窓 0 <= x <= 1 */
static double blackman_window(double x)
{
    return 0.42 - 0.5 * cos(2.0 * R2SAMPLER_PI * x) + 0.08 * cos(4.0 * R2SAMPLER_PI * x);
}

/* Nuttall窓 0 <= x <= 1 */
static double nuttall_window(double x)
{
    return 0.355768 - 0.487396 * cos(2.0 * R2SAMPLER_PI * x) + 0.144232 * cos(4.0 * R2SAMPLER_PI * x) - 0.012604 * cos(6.0 * R2SAMPLER_PI * x);
}

/* Blackman-Nuttall窓 0 <= x <= 1 */
static double blackman_nuttall_window(double x)
{
    return 0.3635819 - 0.4891775 * cos(2.0 * R2SAMPLER_PI * x) + 0.1365995 * cos(4.0 * R2SAMPLER_PI * x) - 0.0106411 * cos(6.0 * R2SAMPLER_PI * x);
}


/* xとyの最大公約数を求める */
uint32_t R2sampler_GCD(uint32_t x, uint32_t y)
{
    uint32_t t;

    while (y != 0) {
        t = x % y;
        x = y;
        y = t;
    }

    return x;
}

/* 指定された個数内で素因数分解を実行（残った因数は末尾に） */
void R2sampler_Factorize(
        uint32_t x, uint32_t *factors, uint32_t max_num_factors, uint32_t *num_factors)
{
    uint32_t d, q, idx;

    assert((factors != NULL) && (num_factors != NULL));

    /* 1分割のケース */
    if (max_num_factors == 1) {
        factors[0] = x;
        (*num_factors) = 1;
        return;
    }

    idx = 0;

    /* 2で割り続ける */
    while ((x >= 4) && ((x % 2) == 0)) {
        factors[idx++] = 2;
        x /= 2;
        if (idx == (max_num_factors - 1)) {
            goto END;
        }
    }

    /* 3以上の素数で試す */
    d = 3;
    q = x / d;
    while (q >= d) {
        if ((x % d) == 0) {
            factors[idx++] = d;
            x = q;
            if (idx == (max_num_factors - 1)) {
                goto END;
            }
        } else {
            d += 2;
        }
        q = x / d;
    }

END:
    /* 残った因数を追加 */
    factors[idx++] = x;
    (*num_factors) = idx;

    assert(idx <= max_num_factors);
}

/* 窓関数によりLPF設計 */
void R2sampler_CreateLPFByWindowFunction(
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
        filter_coef[i] = 2.0f * cutoff * (float)sinc(2.0f * R2SAMPLER_PI * cutoff * x);
    }

    /* フィルタサイズが1の場合は矩形窓に読み替えて終わり */
    if (filter_order == 1) {
        filter_coef[0] *= 1.0f;
        return;
    }

    /* 窓関数適用 */
    switch (window_type) {
    case R2SAMPLERLPF_WINDOW_TYPE_RECTANGULAR:
        /* 矩形窓: 何もせず打ち切り */
        break;
    case R2SAMPLERLPF_WINDOW_TYPE_HANN:
        /* ハン窓 */
        for (i = 0; i < filter_order; i++) {
            filter_coef[i] *= (float)hanning_window(i / (filter_order - 1.0f));
        }
        break;
    case R2SAMPLERLPF_WINDOW_TYPE_BLACKMAN:
        /* Blackman窓 */
        for (i = 0; i < filter_order; i++) {
            filter_coef[i] *= (float)blackman_window(i / (filter_order - 1.0f));
        }
        break;
    case R2SAMPLERLPF_WINDOW_TYPE_NUTTALL:
        /* Nuttall窓 */
        for (i = 0; i < filter_order; i++) {
            filter_coef[i] *= (float)nuttall_window(i / (filter_order - 1.0f));
        }
        break;
    case R2SAMPLERLPF_WINDOW_TYPE_BLACKMANNUTTALL:
        /* Blackman-Nuttall窓 */
        for (i = 0; i < filter_order; i++) {
            filter_coef[i] *= (float)blackman_nuttall_window(i / (filter_order - 1.0f));
        }
        break;
    default:
        assert(0);
    }

}
