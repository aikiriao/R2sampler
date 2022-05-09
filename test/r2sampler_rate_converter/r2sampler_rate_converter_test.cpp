#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/r2sampler_rate_converter/src/r2sampler_rate_converter.c"
}

/* ハンドル作成・破棄テスト */
TEST(R2samplerRateConverterTest, CreateDestroyHandleTest)
{
/* 有効なコンフィグをセット */
#define R2samplerRateConverter_SetValidConfig(p_config)\
    do {\
        struct R2samplerRateConverterConfig *config__p = p_config;\
        config__p->max_num_input_samples    = 32;\
        config__p->input_rate               = 44100;\
        config__p->output_rate              = 48000;\
        config__p->filter_type              = R2SAMPLER_FILTERTYPE_NONE;\
        config__p->filter_order             = 1;\
    } while (0);

    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct R2samplerRateConverterConfig config;

        /* 最低限構造体本体よりは大きいはず */
        R2samplerRateConverter_SetValidConfig(&config);
        work_size = R2samplerRateConverter_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct R2samplerRateConverter));

        /* 不正な引数 */
        EXPECT_TRUE(R2samplerRateConverter_CalculateWorkSize(NULL) < 0);

        /* 不正なコンフィグ */
        R2samplerRateConverter_SetValidConfig(&config);
        config.max_num_input_samples = 0;
        EXPECT_TRUE(R2samplerRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerRateConverter_SetValidConfig(&config);
        config.input_rate = 0;
        EXPECT_TRUE(R2samplerRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerRateConverter_SetValidConfig(&config);
        config.output_rate = 0;
        EXPECT_TRUE(R2samplerRateConverter_CalculateWorkSize(&config) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct R2samplerRateConverter *converter;
        struct R2samplerRateConverterConfig config;

        R2samplerRateConverter_SetValidConfig(&config);
        work_size = R2samplerRateConverter_CalculateWorkSize(&config);
        work = malloc(work_size);

        converter = R2samplerRateConverter_Create(&config, work, work_size);
        ASSERT_TRUE(converter != NULL);
        EXPECT_TRUE(converter->work == work);
        EXPECT_EQ(0, converter->alloc_by_own);
        EXPECT_TRUE(converter->output_buffer != NULL);
        EXPECT_TRUE(converter->interp_buffer != NULL);
        EXPECT_TRUE(converter->filter_coef != NULL);
        EXPECT_EQ(config.filter_order, converter->filter_order);

        R2samplerRateConverter_Destroy(converter);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct R2samplerRateConverter *converter;
        struct R2samplerRateConverterConfig config;

        R2samplerRateConverter_SetValidConfig(&config);

        converter = R2samplerRateConverter_Create(&config, NULL, 0);
        ASSERT_TRUE(converter != NULL);
        EXPECT_TRUE(converter->work != NULL);
        EXPECT_EQ(1, converter->alloc_by_own);
        EXPECT_TRUE(converter->output_buffer != NULL);
        EXPECT_TRUE(converter->interp_buffer != NULL);
        EXPECT_TRUE(converter->filter_coef != NULL);
        EXPECT_EQ(config.filter_order, converter->filter_order);

        R2samplerRateConverter_Destroy(converter);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct R2samplerRateConverter *converter;
        struct R2samplerRateConverterConfig config;

        R2samplerRateConverter_SetValidConfig(&config);
        work_size = R2samplerRateConverter_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        converter = R2samplerRateConverter_Create(NULL, work, work_size);
        EXPECT_TRUE(converter == NULL);
        converter = R2samplerRateConverter_Create(&config, NULL, work_size);
        EXPECT_TRUE(converter == NULL);
        converter = R2samplerRateConverter_Create(&config, work, 0);
        EXPECT_TRUE(converter == NULL);

        /* ワークサイズ不足 */
        converter = R2samplerRateConverter_Create(&config, work, work_size - 1);
        EXPECT_TRUE(converter == NULL);

        /* コンフィグが不正 */
        R2samplerRateConverter_SetValidConfig(&config);
        config.max_num_input_samples = 0;
        converter = R2samplerRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerRateConverter_SetValidConfig(&config);
        config.input_rate = 0;
        converter = R2samplerRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerRateConverter_SetValidConfig(&config);
        config.output_rate = 0;
        converter = R2samplerRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct R2samplerRateConverter *converter;
        struct R2samplerRateConverterConfig config;

        R2samplerRateConverter_SetValidConfig(&config);

        /* 引数が不正 */
        converter = R2samplerRateConverter_Create(NULL, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        /* コンフィグが不正 */
        R2samplerRateConverter_SetValidConfig(&config);
        config.max_num_input_samples = 0;
        converter = R2samplerRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerRateConverter_SetValidConfig(&config);
        config.input_rate = 0;
        converter = R2samplerRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerRateConverter_SetValidConfig(&config);
        config.output_rate = 0;
        converter = R2samplerRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);
    }
}

/* レート変換テスト */
TEST(R2samplerRateConverterTest, RateConvertTest)
{
    /* rate倍に補間 */
    {
#define MAXRATE 16
#define NUMSAMPLES 16
#define NUMINPUTS 1
        struct R2samplerRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLERRATECONVERTER_MAX_NUM_OUTPUT_SAMPLES(NUMSAMPLES, 1, rate);
            uint32_t in_prog, out_prog, smpl;
            struct R2samplerRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.max_num_input_samples = NUMINPUTS;
            config.input_rate = 1;
            config.output_rate = rate;
            config.filter_type = R2SAMPLER_FILTERTYPE_NONE;
            config.filter_order = 1;
            converter = R2samplerRateConverter_Create(&config, NULL, 0);
            ASSERT_TRUE(converter != NULL);

            EXPECT_EQ(converter->up_rate, rate);
            EXPECT_EQ(converter->down_rate, 1);

            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                input[smpl] = 1.0f;
            }
            for (smpl = 0; smpl < num_buffer_samples; smpl++) {
                output[smpl] = 0.0f;
            }

            in_prog = out_prog = 0;
            while (in_prog < NUMSAMPLES) {
                uint32_t num_outputs;
                ret = R2samplerRateConverter_Process(converter,
                        &input[in_prog], NUMINPUTS,
                        &output[out_prog], num_buffer_samples - out_prog, &num_outputs);
                ASSERT_EQ(R2SAMPLERRATECONVERTER_APIRESULT_OK, ret);

                in_prog += NUMINPUTS;
                out_prog += num_outputs;
            }

            EXPECT_EQ(out_prog, rate * NUMSAMPLES);

            /* rate倍に間引かれて1.0fが配置されているはず */
            for (smpl = 0; smpl < rate * NUMSAMPLES; smpl++) {
                if ((smpl % rate) == 0) {
                    EXPECT_FLOAT_EQ(1.0f, output[smpl]);
                } else {
                    EXPECT_FLOAT_EQ(0.0f, output[smpl]);
                }
            }

            R2samplerRateConverter_Destroy(converter);
            free(output);
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }

    /* rate倍に間引き */
    {
#define MAXRATE 16
#define NUMSAMPLES 16
#define NUMINPUTS 1
        struct R2samplerRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[MAXRATE * NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLERRATECONVERTER_MAX_NUM_OUTPUT_SAMPLES(rate * NUMSAMPLES, rate, 1);
            uint32_t in_prog, out_prog, smpl;
            struct R2samplerRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.max_num_input_samples = rate * NUMINPUTS;
            config.input_rate = rate;
            config.output_rate = 1;
            config.filter_type = R2SAMPLER_FILTERTYPE_NONE;
            config.filter_order = 1;
            converter = R2samplerRateConverter_Create(&config, NULL, 0);
            ASSERT_TRUE(converter != NULL);

            EXPECT_EQ(converter->up_rate, 1);
            EXPECT_EQ(converter->down_rate, rate);

            for (smpl = 0; smpl < MAXRATE * NUMSAMPLES; smpl++) {
                input[smpl] = 0.0f;
            }
            /* rate間隔で1.0fを配置 */
            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                input[rate * smpl] = 1.0f;
            }
            for (smpl = 0; smpl < num_buffer_samples; smpl++) {
                output[smpl] = 0.0f;
            }

            in_prog = out_prog = 0;
            while (in_prog < rate * NUMSAMPLES) {
                uint32_t num_outputs;
                ret = R2samplerRateConverter_Process(converter,
                        &input[in_prog], NUMINPUTS,
                        &output[out_prog], num_buffer_samples - out_prog, &num_outputs);
                assert(R2SAMPLERRATECONVERTER_APIRESULT_OK == ret);
                ASSERT_EQ(R2SAMPLERRATECONVERTER_APIRESULT_OK, ret);

                in_prog += NUMINPUTS;
                out_prog += num_outputs;
            }

            EXPECT_EQ(out_prog, NUMSAMPLES);

            /* 全て1.0fになるはず */
            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                EXPECT_FLOAT_EQ(1.0f, output[smpl]);
            }

            R2samplerRateConverter_Destroy(converter);
            free(output);
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }

    /* 有理数倍に補間/間引き */
    {
#define MAXRATE  32
#define NUMSAMPLES 32
#define NUMINPUTS 1
        struct R2samplerRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[MAXRATE * NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, in_rate, out_rate;

        for (in_rate = 1; in_rate <= MAXRATE; in_rate++) {
            for (out_rate = 1; out_rate <= MAXRATE; out_rate++) {
                const uint32_t num_buffer_samples
                    = R2SAMPLERRATECONVERTER_MAX_NUM_OUTPUT_SAMPLES(in_rate * NUMSAMPLES, in_rate, out_rate);
                uint32_t in_prog, out_prog, smpl;
                struct R2samplerRateConverterConfig config;

                /* 互いに素ではないケースは弾く */
                if (R2sampler_GCD(in_rate, out_rate) != 1) {
                    continue;
                }

                output = (float *)malloc(sizeof(float) * num_buffer_samples);

                config.max_num_input_samples = in_rate * NUMINPUTS;
                config.input_rate = in_rate;
                config.output_rate = out_rate;
                config.filter_type = R2SAMPLER_FILTERTYPE_NONE;
                config.filter_order = 1;

                converter = R2samplerRateConverter_Create(&config, NULL, 0);
                ASSERT_TRUE(converter != NULL);

                for (smpl = 0; smpl < MAXRATE * NUMSAMPLES; smpl++) {
                    input[smpl] = 1.0f;
                }
                for (smpl = 0; smpl < num_buffer_samples; smpl++) {
                    output[smpl] = 0.0f;
                }

                in_prog = out_prog = 0;
                while (in_prog < in_rate * NUMSAMPLES) {
                    uint32_t num_outputs;
                    ret = R2samplerRateConverter_Process(converter,
                            &input[in_prog], NUMINPUTS,
                            &output[out_prog], num_buffer_samples - out_prog, &num_outputs);
                    ASSERT_EQ(R2SAMPLERRATECONVERTER_APIRESULT_OK, ret);

                    in_prog += NUMINPUTS;
                    out_prog += num_outputs;
                }

                EXPECT_EQ(out_prog, out_rate * NUMSAMPLES);

                /* in_rate / out_rate の間隔で1.0fが並ぶはず */
                for (smpl = 0; smpl < out_rate * NUMSAMPLES; smpl++) {
                    if (((smpl * in_rate) % out_rate) == 0) {
                        EXPECT_FLOAT_EQ(1.0f, output[smpl]);
                    } else {
                        EXPECT_FLOAT_EQ(0.0f, output[smpl]);
                    }
                }

                R2samplerRateConverter_Destroy(converter);
                free(output);
            }
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }

    /* rate倍に補間（LPF使用） */
    {
#define MAXRATE 16
#define NUMSAMPLES 16
#define NUMINPUTS 1
        struct R2samplerRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLERRATECONVERTER_MAX_NUM_OUTPUT_SAMPLES(NUMSAMPLES, 1, rate);
            uint32_t in_prog, out_prog, smpl, delay;
            struct R2samplerRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.max_num_input_samples = NUMINPUTS;
            config.input_rate = 1;
            config.output_rate = rate;
            config.filter_type = R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW;
            config.filter_order = 3;
            converter = R2samplerRateConverter_Create(&config, NULL, 0);
            ASSERT_TRUE(converter != NULL);

            EXPECT_EQ(converter->up_rate, rate);
            EXPECT_EQ(converter->down_rate, 1);

            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                input[smpl] = 1.0f;
            }
            for (smpl = 0; smpl < num_buffer_samples; smpl++) {
                output[smpl] = 0.0f;
            }

            in_prog = out_prog = 0;
            while (in_prog < NUMSAMPLES) {
                uint32_t num_outputs;
                ret = R2samplerRateConverter_Process(converter,
                        &input[in_prog], NUMINPUTS,
                        &output[out_prog], num_buffer_samples - out_prog, &num_outputs);
                ASSERT_EQ(R2SAMPLERRATECONVERTER_APIRESULT_OK, ret);

                in_prog += NUMINPUTS;
                out_prog += num_outputs;
            }

            EXPECT_EQ(out_prog, rate * NUMSAMPLES);

            /* rate倍に間引かれて1.0fが配置されているはず（フィルタ遅延込みで） */
            delay = config.filter_order / 2;
            for (smpl = 0; smpl < rate * NUMSAMPLES - delay; smpl++) {
                if ((smpl % rate) == 0) {
                    EXPECT_FLOAT_EQ(1.0f, output[smpl + delay]);
                } else {
                    EXPECT_FLOAT_EQ(0.0f, output[smpl + delay]);
                }
            }

            R2samplerRateConverter_Destroy(converter);
            free(output);
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }

    /* rate倍に間引き（LPF使用） */
    {
#define MAXRATE 16
#define NUMSAMPLES 16
#define NUMINPUTS 1
        struct R2samplerRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[MAXRATE * NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLERRATECONVERTER_MAX_NUM_OUTPUT_SAMPLES(rate * NUMSAMPLES, rate, 1);
            uint32_t in_prog, out_prog, smpl, delay;
            struct R2samplerRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.max_num_input_samples = rate * NUMINPUTS;
            config.input_rate = rate;
            config.output_rate = 1;
            config.filter_type = R2SAMPLER_FILTERTYPE_LPF_HANNWINDOW;
            config.filter_order = 1;
            converter = R2samplerRateConverter_Create(&config, NULL, 0);
            ASSERT_TRUE(converter != NULL);

            EXPECT_EQ(converter->up_rate, 1);
            EXPECT_EQ(converter->down_rate, rate);

            for (smpl = 0; smpl < MAXRATE * NUMSAMPLES; smpl++) {
                input[smpl] = 0.0f;
            }
            /* rate間隔で1.0fを配置 */
            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                input[rate * smpl] = 1.0f;
            }
            for (smpl = 0; smpl < num_buffer_samples; smpl++) {
                output[smpl] = 0.0f;
            }

            in_prog = out_prog = 0;
            while (in_prog < rate * NUMSAMPLES) {
                uint32_t num_outputs;
                ret = R2samplerRateConverter_Process(converter,
                        &input[in_prog], NUMINPUTS,
                        &output[out_prog], num_buffer_samples - out_prog, &num_outputs);
                assert(R2SAMPLERRATECONVERTER_APIRESULT_OK == ret);
                ASSERT_EQ(R2SAMPLERRATECONVERTER_APIRESULT_OK, ret);

                in_prog += NUMINPUTS;
                out_prog += num_outputs;
            }

            EXPECT_EQ(out_prog, NUMSAMPLES);

            /* LPF係数が乗算され1/rateのみになる */
            for (smpl = 0; smpl < NUMSAMPLES; smpl++) {
                EXPECT_FLOAT_EQ(1.0f / rate, output[smpl]);
            }

            R2samplerRateConverter_Destroy(converter);
            free(output);
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }

}
