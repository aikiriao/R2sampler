#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/r2sampler_rate_converter/src/r2sampler_multi_stage_rate_converter.c"
}

/* ハンドル作成・破棄テスト */
TEST(R2samplerMultiStageRateConverterTest, CreateDestroyHandleTest)
{
/* 有効なコンフィグをセット */
#define R2samplerMultiStageRateConverter_SetValidConfig(p_config)\
    do {\
        struct R2samplerMultiStageRateConverterConfig *config__p = p_config;\
        config__p->single.max_num_input_samples = 32;\
        config__p->single.input_rate            = 44100;\
        config__p->single.output_rate           = 48000;\
        config__p->single.filter_type           = R2SAMPLER_FILTERTYPE_NONE;\
        config__p->single.filter_order          = 1;\
        config__p->max_num_stages               = 4;\
    } while (0);

    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct R2samplerMultiStageRateConverterConfig config;

        /* 最低限構造体本体よりは大きいはず */
        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        work_size = R2samplerMultiStageRateConverter_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size > sizeof(struct R2samplerMultiStageRateConverter));

        /* 不正な引数 */
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(NULL) < 0);

        /* 不正なコンフィグ */
        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.max_num_input_samples = 0;
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.input_rate = 0;
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.output_rate = 0;
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = 0;
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(&config) < 0);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = R2SAMPLER_MAX_NUM_STAGES + 1;
        EXPECT_TRUE(R2samplerMultiStageRateConverter_CalculateWorkSize(&config) < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct R2samplerMultiStageRateConverter *converter;
        struct R2samplerMultiStageRateConverterConfig config;

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        work_size = R2samplerMultiStageRateConverter_CalculateWorkSize(&config);
        work = malloc(work_size);

        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        ASSERT_TRUE(converter != NULL);
        EXPECT_TRUE(converter->work == work);
        EXPECT_EQ(0, converter->alloc_by_own);
        EXPECT_TRUE(converter->resampler != NULL);
        EXPECT_TRUE(converter->process_buffer[0] != NULL);
        EXPECT_TRUE(converter->process_buffer[1] != NULL);
        EXPECT_EQ(config.max_num_stages, converter->max_num_stages);

        R2samplerMultiStageRateConverter_Destroy(converter);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct R2samplerMultiStageRateConverter *converter;
        struct R2samplerMultiStageRateConverterConfig config;

        R2samplerMultiStageRateConverter_SetValidConfig(&config);

        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        ASSERT_TRUE(converter != NULL);
        EXPECT_TRUE(converter->work != NULL);
        EXPECT_EQ(1, converter->alloc_by_own);
        EXPECT_TRUE(converter->resampler != NULL);
        EXPECT_TRUE(converter->process_buffer[0] != NULL);
        EXPECT_TRUE(converter->process_buffer[1] != NULL);
        EXPECT_EQ(config.max_num_stages, converter->max_num_stages);

        R2samplerMultiStageRateConverter_Destroy(converter);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct R2samplerMultiStageRateConverter *converter;
        struct R2samplerMultiStageRateConverterConfig config;

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        work_size = R2samplerMultiStageRateConverter_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        converter = R2samplerMultiStageRateConverter_Create(NULL, work, work_size);
        EXPECT_TRUE(converter == NULL);
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, work_size);
        EXPECT_TRUE(converter == NULL);
        converter = R2samplerMultiStageRateConverter_Create(&config, work, 0);
        EXPECT_TRUE(converter == NULL);

        /* ワークサイズ不足 */
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size - 1);
        EXPECT_TRUE(converter == NULL);

        /* コンフィグが不正 */
        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.max_num_input_samples = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.input_rate = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.output_rate = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = R2SAMPLER_MAX_NUM_STAGES + 1;
        converter = R2samplerMultiStageRateConverter_Create(&config, work, work_size);
        EXPECT_TRUE(converter == NULL);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct R2samplerMultiStageRateConverter *converter;
        struct R2samplerMultiStageRateConverterConfig config;

        R2samplerMultiStageRateConverter_SetValidConfig(&config);

        /* 引数が不正 */
        converter = R2samplerMultiStageRateConverter_Create(NULL, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        /* コンフィグが不正 */
        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.max_num_input_samples = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.input_rate = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.single.output_rate = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = 0;
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);

        R2samplerMultiStageRateConverter_SetValidConfig(&config);
        config.max_num_stages = R2SAMPLER_MAX_NUM_STAGES + 1;
        converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
        EXPECT_TRUE(converter == NULL);
    }
}

/* レート変換テスト */
TEST(R2samplerMultiStageRateConverterTest, RateConvertTest)
{
    /* rate倍に補間 */
    {
#define MAXRATE 16
#define NUMSAMPLES 16
#define NUMINPUTS 1
        struct R2samplerMultiStageRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(NUMSAMPLES, 1, rate);
            uint32_t in_prog, out_prog, smpl;
            struct R2samplerMultiStageRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.single.max_num_input_samples = NUMINPUTS;
            config.single.input_rate = 1;
            config.single.output_rate = rate;
            config.single.filter_type = R2SAMPLER_FILTERTYPE_NONE;
            config.single.filter_order = 1;
            config.max_num_stages = 2;
            converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
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
                ret = R2samplerMultiStageRateConverter_Process(converter,
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

            R2samplerMultiStageRateConverter_Destroy(converter);
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
        struct R2samplerMultiStageRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[MAXRATE * NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, rate;

        for (rate = 1; rate <= MAXRATE; rate++) {
            const uint32_t num_buffer_samples
                = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(rate * NUMSAMPLES, rate, 1);
            uint32_t in_prog, out_prog, smpl;
            struct R2samplerMultiStageRateConverterConfig config;

            output = (float *)malloc(sizeof(float) * num_buffer_samples);

            config.single.max_num_input_samples = rate * NUMINPUTS;
            config.single.input_rate = rate;
            config.single.output_rate = 1;
            config.single.filter_type = R2SAMPLER_FILTERTYPE_NONE;
            config.single.filter_order = 1;
            config.max_num_stages = 2;
            converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
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
                ret = R2samplerMultiStageRateConverter_Process(converter,
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

            R2samplerMultiStageRateConverter_Destroy(converter);
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
        struct R2samplerMultiStageRateConverter *converter;
        R2samplerRateConverterApiResult ret;
        float input[MAXRATE * NUMSAMPLES];
        float *output;
        uint32_t in_prog, out_prog, smpl, in_rate, out_rate;

        for (in_rate = 1; in_rate <= MAXRATE; in_rate++) {
            for (out_rate = 1; out_rate <= MAXRATE; out_rate++) {
                const uint32_t num_buffer_samples
                    = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(in_rate * NUMSAMPLES, in_rate, out_rate);
                uint32_t in_prog, out_prog, smpl;
                struct R2samplerMultiStageRateConverterConfig config;

                /* 互いに素ではないケースは弾く */
                if (R2sampler_GCD(in_rate, out_rate) != 1) {
                    continue;
                }

                output = (float *)malloc(sizeof(float) * num_buffer_samples);

                config.single.max_num_input_samples = in_rate * NUMINPUTS;
                config.single.input_rate = in_rate;
                config.single.output_rate = out_rate;
                config.single.filter_type = R2SAMPLER_FILTERTYPE_NONE;
                config.single.filter_order = 1;
                config.max_num_stages = 2;

                converter = R2samplerMultiStageRateConverter_Create(&config, NULL, 0);
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
                    ret = R2samplerMultiStageRateConverter_Process(converter,
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

                R2samplerMultiStageRateConverter_Destroy(converter);
                free(output);
            }
        }
#undef MAXRATE
#undef NUMSAMPLES
#undef NUMINPUTS
    }
}
