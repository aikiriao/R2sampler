#include "fft.h"

#include <string.h>
#include <math.h>
#include <assert.h>

/* 円周率 */
#define _PI 3.14159265358979323846

/* 複素数型 */
typedef struct FFTComplex {
    float real; /* 実部 */
    float imag; /* 虚部 */
} FFTComplex;

/* FFT 正規化は行いません
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力)
* y 作業用配列(xと同一サイズ)
*/
static void FFT_ComplexFFT(int n, int flag, FFTComplex *x, FFTComplex *y);

/* 複素数型のサイズチェック floatの配列を複素数型とみなして計算するため
* 構造体にパディングなどが入ってしまうとサイズが合わなくなる
* 合わない場合は#pragmaで構造体をパックする */
extern char FFT_checksize[(sizeof(FFTComplex) == (sizeof(float) * 2)) ? 1 : -1];

/* 複素数加算 インライン展開を期待するためstatic関数 */
static FFTComplex FFTComplex_Add(FFTComplex a, FFTComplex b)
{
    FFTComplex ret;
    ret.real = a.real + b.real;
    ret.imag = a.imag + b.imag;
    return ret;
}

/* 複素数減算 インライン展開を期待するためstatic関数 */
static FFTComplex FFTComplex_Sub(FFTComplex a, FFTComplex b)
{
    FFTComplex ret;
    ret.real = a.real - b.real;
    ret.imag = a.imag - b.imag;
    return ret;
}

/* 複素数乗算 インライン展開を期待するためstatic関数 */
static FFTComplex FFTComplex_Mul(FFTComplex a, FFTComplex b)
{
    FFTComplex ret;
    ret.real = a.real * b.real - a.imag * b.imag;
    ret.imag = a.real * b.imag + a.imag * b.real;
    return ret;
}

/* FFT 正規化は行いません
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力)
* y 作業用配列(xと同一サイズ)
*/
static void FFT_ComplexFFT(int n, const int flag, FFTComplex *x, FFTComplex *y)
{
    FFTComplex *tmp, *src = x;
    int p, q;
    int s = 1; /* ストライド */

    /* 4基底 Stockham FFT */
    while (n > 2) {
        const int n1 = (n >> 2);
        const int n2 = (n >> 1);
        const int n3 = n1 + n2;
        const float theta0 = (float)(2.0 * _PI / n);
        FFTComplex j, wdelta;
        j.real = 0.0; j.imag = flag;
        wdelta.real = cosf(theta0); wdelta.imag = -flag * sinf(theta0);
        for (p = 0; p < n1; p++) {
            const FFTComplex w1p = { cosf(p * theta0), flag * sinf(p * theta0) };
            const FFTComplex w2p = FFTComplex_Mul(w1p, w1p);
            const FFTComplex w3p = FFTComplex_Mul(w1p, w2p);
            for (q = 0; q < s; q++) {
                const FFTComplex    a = x[q + s * (p +  0)];
                const FFTComplex    b = x[q + s * (p + n1)];
                const FFTComplex    c = x[q + s * (p + n2)];
                const FFTComplex    d = x[q + s * (p + n3)];
                const FFTComplex  apc = FFTComplex_Add(a, c);
                const FFTComplex  amc = FFTComplex_Sub(a, c);
                const FFTComplex  bpd = FFTComplex_Add(b, d);
                const FFTComplex jbmd = FFTComplex_Mul(j, FFTComplex_Sub(b, d));
                y[q + s * ((p << 2) + 0)] = FFTComplex_Add(apc, bpd);
                y[q + s * ((p << 2) + 1)] = FFTComplex_Mul(w1p, FFTComplex_Sub(amc, jbmd));
                y[q + s * ((p << 2) + 2)] = FFTComplex_Mul(w2p, FFTComplex_Sub(apc,  bpd));
                y[q + s * ((p << 2) + 3)] = FFTComplex_Mul(w3p, FFTComplex_Add(amc, jbmd));
            }
        }
        n >>= 2;
        s <<= 2;
        tmp = x; x = y; y = tmp;
    }

    if (n == 2) {
        for (q = 0; q < s; q++) {
            const FFTComplex a = x[q + 0];
            const FFTComplex b = x[q + s];
            y[q + 0] = FFTComplex_Add(a, b);
            y[q + s] = FFTComplex_Sub(a, b);
        }
        s <<= 1;
        tmp = x; x = y; y = tmp;
    }

    if (src != x) {
        memcpy(y, x, sizeof(FFTComplex) * (size_t)s);
    }
}

/* FFT 正規化は行いません
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力 2nサイズ必須, 偶数番目に実数部, 奇数番目に虚数部)
* y 作業用配列(xと同一サイズ)
*/
void FFT_FloatFFT(int n, const int flag, float *x, float *y)
{
    FFT_ComplexFFT(n, flag, (FFTComplex *)x, (FFTComplex *)y);
}

/* 実数列のFFT 正規化は行いません 正規化定数は2/n
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力 nサイズ必須, FFTの場合, x[0]に直流成分の実部, x[1]に最高周波数成分の虚数部が入る)
* y 作業用配列(xと同一サイズ)
*/
void FFT_RealFFT(int n, const int flag, float *x, float *y)
{
    int i;
    const float theta = (float)(-flag * 2.0 * _PI / n);
    const float wpi = sinf(theta);
    const float wpr = cosf(theta) - 1.0f;
    const float c2 = (float)flag * 0.5f;
    float wr, wi, wtmp;

    /* FFTの場合は先に順変換 */
    if (flag == -1) {
        FFT_FloatFFT(n >> 1, -1, x, y);
    }

    /* 回転因子初期化 */
    wr = 1.0f + wpr;
    wi = wpi;

    /* スペクトルの対称性を使用し */
    /* FFTの場合は最終結果をまとめ、IFFTの場合は元に戻るよう整理 */
    for (i = 1; i < (n >> 2); i++) {
        const int i1 = (i << 1);
        const int i2 = i1 + 1;
        const int i3 = n - i1;
        const int i4 = i3 + 1;
        const float h1r = 0.5f * (x[i1] + x[i3]);
        const float h1i = 0.5f * (x[i2] - x[i4]);
        const float h2r =  -c2 * (x[i2] + x[i4]);
        const float h2i =   c2 * (x[i1] - x[i3]);
        x[i1] =  h1r + (wr * h2r) - (wi * h2i);
        x[i2] =  h1i + (wr * h2i) + (wi * h2r);
        x[i3] =  h1r - (wr * h2r) + (wi * h2i);
        x[i4] = -h1i + (wr * h2i) + (wi * h2r);
        /* 回転因子更新 */
        wtmp = wr;
        wr += wtmp * wpr - wi * wpi;
        wi += wi * wpr + wtmp * wpi;
    }

    /* 直流成分/最高周波数成分 */
    {
        const float h1r = x[0];
        if (flag == -1) {
            x[0] = h1r + x[1];
            x[1] = h1r - x[1];
        } else {
            x[0] = 0.5f * (h1r + x[1]);
            x[1] = 0.5f * (h1r - x[1]);
            FFT_FloatFFT(n >> 1, 1, x, y);
        }
    }
}
