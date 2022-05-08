#ifndef R2SAMPLER_RATE_CONVERTER_H_INCLUDED
#define R2SAMPLER_RATE_CONVERTER_H_INCLUDED

#include <stdint.h>
#include <r2sampler.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* 内部でバッファリングしているサンプル数を取得 */
uint32_t R2samplerRateConverter_GetNumBufferedSamples(const struct R2samplerRateConverter *converter);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* R2SAMPLER_RATE_CONVERTER_H_INCLUDED */

