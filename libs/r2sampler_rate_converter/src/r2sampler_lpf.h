#ifndef R2SAMPLER_LPF_H_INCLUDED
#define R2SAMPLER_LPF_H_INCLUDED

#include <stdint.h>

/* 窓関数タイプ */
typedef enum R2samplerLPFWindowType {
    R2SAMPLERLPF_WINDOW_TYPE_RECTANGULAR = 0, /* 矩形窓 */
    R2SAMPLERLPF_WINDOW_TYPE_HANN /* ハン窓 */
} R2samplerLPFWindowType;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* 窓関数によりLPF設計 */
void R2samplerLPF_CreateLPFByWindowFunction(
        float cutoff, R2samplerLPFWindowType window_type,
        float *filter_coef, uint32_t filter_order);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* R2SAMPLER_LPF_H_INCLUDED */
