#ifndef R2SAMPLER_UTILITY_H_INCLUDED
#define R2SAMPLER_UTILITY_H_INCLUDED

#include <stdint.h>

/* 窓関数タイプ */
typedef enum R2samplerLPFWindowType {
    R2SAMPLERLPF_WINDOW_TYPE_RECTANGULAR = 0,   /* 矩形窓 */
    R2SAMPLERLPF_WINDOW_TYPE_HANN,              /* ハン窓 */
    R2SAMPLERLPF_WINDOW_TYPE_BLACKMAN,          /* Blackman窓 */
    R2SAMPLERLPF_WINDOW_TYPE_NUTTALL,           /* Nuttall窓 */
    R2SAMPLERLPF_WINDOW_TYPE_INVALID            /* 無効値 */
} R2samplerLPFWindowType;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* xとyの最大公約数を求める */
uint32_t R2sampler_GCD(uint32_t x, uint32_t y);

/* 指定された個数内で素因数分解を実行（残った因数は末尾に） */
void R2sampler_Factorize(
        uint32_t x, uint32_t *factors, uint32_t max_num_factors, uint32_t *num_factors);

/* 窓関数によりLPF設計 */
void R2sampler_CreateLPFByWindowFunction(
        float cutoff, R2samplerLPFWindowType window_type,
        float *filter_coef, uint32_t filter_order);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* R2SAMPLER_UTILITY_H_INCLUDED */
