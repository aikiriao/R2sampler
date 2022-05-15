#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include <r2sampler.h>
#include "wav.h"
#include "command_line_parser.h"

/* 最小値の選択 */
#define RSAMPLER_MIN(a, b) (((a) < (b)) ? (a) : (b))
/* 最大値の選択 */
#define RSAMPLER_MAX(a, b) (((a) > (b)) ? (a) : (b))
/* 範囲内にクリップ */
#define RSAMPLER_INNER_VAL(val, min, max) RSAMPLER_MIN(max, RSAMPLER_MAX(min, val))

/* コマンドライン仕様 */
static struct CommandLineParserSpecification command_line_spec[] = {
    { 'r', "output-rate", COMMAND_LINE_PARSER_TRUE,
        "Specify output sampling rate",
        NULL, COMMAND_LINE_PARSER_FALSE },
    { 'b', "buffer-size", COMMAND_LINE_PARSER_TRUE,
        "Specify process buffer size. (default:128)",
        "128", COMMAND_LINE_PARSER_TRUE },
    { 'q', "quality", COMMAND_LINE_PARSER_TRUE,
        "Specify resampling quality. 0:low(fast) - 9:high(slow) (default:5)",
        "5", COMMAND_LINE_PARSER_TRUE },
    { 'h', "help", COMMAND_LINE_PARSER_FALSE,
        "Show command help message",
        NULL, COMMAND_LINE_PARSER_FALSE },
    { 'v', "version", COMMAND_LINE_PARSER_FALSE,
        "Show version information",
        NULL, COMMAND_LINE_PARSER_FALSE },
    { 0, }
};

/* round関数 */
static double myround(double f)
{
    return (f >= 0.0) ? floor(f + 0.5) : -floor(-f + 0.5);
}

/* レート変換実行 */
static int do_rate_convert(
        const char *input_file, const char *output_file,
        uint32_t output_rate, uint32_t num_buffer_samples, uint32_t quality)
{
    uint32_t ch, num_channels, num_samples, num_output_buffer_samples;
    struct WAVFile *inwav, *outwav;
    struct WAVFileFormat outformat;
    float *input_buffer, *output_buffer;
    struct R2samplerMultiStageRateConverter **srcs;

    /* 入力wavファイルを開く */
    if ((inwav = WAV_CreateFromFile(input_file)) == NULL) {
        fprintf(stderr, "Failed to open wav file. \n");
        return 1;
    }

    /* 入力wavのフォーマット取得 */
    num_channels = inwav->format.num_channels;
    num_samples = inwav->format.num_samples;

    /* 出力wavのフォーマット設定 */
    outformat = inwav->format;
    outformat.sampling_rate = output_rate;
    outformat.num_samples = (uint32_t)(((uint64_t)num_samples * output_rate) / inwav->format.sampling_rate);

    /* 出力wavファイル作成 */
    outwav = WAV_Create(&outformat);

    /* 変換バッファ作成 */
    input_buffer = (float *)malloc(sizeof(float) * num_buffer_samples);
    num_output_buffer_samples = R2SAMPLER_MAX_NUM_OUTPUT_SAMPLES(num_buffer_samples, inwav->format.sampling_rate, output_rate);
    output_buffer = (float *)malloc(sizeof(float) * num_output_buffer_samples);

    /* レート変換器作成 */
    {
        struct R2samplerMultiStageRateConverterConfig config;
        config.single.max_num_input_samples = num_buffer_samples;
        config.single.input_rate = inwav->format.sampling_rate;
        config.single.output_rate = output_rate;
        config.single.filter_type = R2SAMPLER_FILTERTYPE_LPF_BLACKMANWINDOW;
        config.single.filter_order = 11 + quality * 20;
        config.max_num_stages = R2SAMPLER_MAX_NUM_STAGES;

        srcs = (struct R2samplerMultiStageRateConverter **)malloc(sizeof(struct R2samplerMultiStageRateConverter *) * num_channels);
        for (ch = 0; ch < num_channels; ch++) {
            if ((srcs[ch] = R2samplerMultiStageRateConverter_Create(&config, NULL, 0)) == NULL) {
                fprintf(stderr, "Failed to create converter handle. \n");
                return 1;
            }
        }
    }

    /* レート変換 */
    for (ch = 0; ch < num_channels; ch++) {
        uint32_t in_progress, out_progress;
        in_progress = out_progress = 0;
        R2samplerMultiStageRateConverter_Start(srcs[ch]);
        while (in_progress < num_samples) {
            uint32_t smpl, num_process_samples, num_output_samples;
            R2samplerRateConverterApiResult ret;
            /* 処理サンプル数 */
            num_process_samples = RSAMPLER_MIN(num_buffer_samples, num_samples - in_progress);
            /* floatに変換 */
            for (smpl = 0; smpl < num_process_samples; smpl++) {
                input_buffer[smpl] = (float)(WAVFile_PCM(inwav, in_progress + smpl, ch) * pow(2.0f, -31));
            }
            /* レート変換処理 */
            if ((ret = R2samplerMultiStageRateConverter_Process(srcs[ch],
                    input_buffer, num_process_samples,
                    output_buffer, num_output_buffer_samples, &num_output_samples)) != R2SAMPLERRATECONVERTER_APIRESULT_OK) {
                fprintf(stderr, "Failed to process rate conversion. (api ret:%d) \n", ret);
                return 1;
            }
            /* 結果を整数に丸め込み */
            for (smpl = 0; smpl < num_output_samples; smpl++) {
                const int64_t pcm = (int64_t)myround(output_buffer[smpl] * pow(2.0f, 31));
                WAVFile_PCM(outwav, out_progress + smpl, ch) = (int32_t)RSAMPLER_INNER_VAL(pcm, INT32_MIN, INT32_MAX);
            }
            in_progress += num_process_samples;
            out_progress += num_output_samples;
            /* 進捗表示 */
            if (in_progress % (num_buffer_samples * 50) == 0) {
                printf("progress... %5.2f%% \r",
                        ((in_progress + ch * num_samples) * 100.0f) / (num_channels * num_samples));
                fflush(stdout);
            }
        }
        assert(out_progress <= outwav->format.num_samples);
    }

    /* 結果出力 */
    if (WAV_WriteToFile(output_file, outwav) != WAV_APIRESULT_OK) {
        fprintf(stderr, "Failed to write file. \n");
        return 1;
    }

    printf("finished!                                \n");

    /* リソース破棄 */
    for (ch = 0; ch < num_channels; ch++) {
        R2samplerMultiStageRateConverter_Destroy(srcs[ch]);
    }
    free(srcs);
    free(output_buffer);
    free(input_buffer);
    WAV_Destroy(outwav);
    WAV_Destroy(inwav);

    return 0;
}

/* 使用法の表示 */
static void print_usage(char** argv)
{
    printf("Usage: %s [options] INPUT_FILE_NAME OUTPUT_FILE_NAME \n", argv[0]);
}

/* バージョン情報の表示 */
static void print_version_info(void)
{
    printf("Rsampler -- Wav file Re-sampler Version.%d \n", R2SAMPLER_VERSION);
}

/* メインエントリ */
int main(int argc, char** argv)
{
    const char* filename_ptr[2] = { NULL, NULL };
    const char* input_file;
    const char* output_file;
    uint32_t num_buffer_samples, output_rate, quality;

    /* 引数が足らない */
    if (argc == 1) {
        print_usage(argv);
        /* 初めて使った人が詰まらないようにヘルプの表示を促す */
        printf("Type `%s -h` to display command helps. \n", argv[0]);
        return 1;
    }

    /* コマンドライン解析 */
    if (CommandLineParser_ParseArguments(command_line_spec,
                argc, (const char* const*)argv, filename_ptr, sizeof(filename_ptr) / sizeof(filename_ptr[0]))
            != COMMAND_LINE_PARSER_RESULT_OK) {
        return 1;
    }

    /* ヘルプやバージョン情報の表示判定 */
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "help") == COMMAND_LINE_PARSER_TRUE) {
        print_usage(argv);
        printf("options: \n");
        CommandLineParser_PrintDescription(command_line_spec);
        return 0;
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "version") == COMMAND_LINE_PARSER_TRUE) {
        print_version_info();
        return 0;
    }

    /* 入力ファイル名の取得 */
    if ((input_file = filename_ptr[0]) == NULL) {
        fprintf(stderr, "%s: input file must be specified. \n", argv[0]);
        return 1;
    }

    /* 出力ファイル名の取得 */
    if ((output_file = filename_ptr[1]) == NULL) {
        fprintf(stderr, "%s: output file must be specified. \n", argv[0]);
        return 1;
    }

    /* 出力レートの取得 */
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "output-rate") == COMMAND_LINE_PARSER_FALSE) {
        fprintf(stderr, "%s: output-rate must be specified. \n", argv[0]);
        return 1;
    }
    /* 出力レート文字列のパース */
    {
        char *e;
        const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "output-rate");
        output_rate = (uint32_t)strtol(lstr, &e, 10);
        if (*e != '\0') {
            fprintf(stderr, "%s: invalid output sampling rate. (irregular character found in %s at %s)\n", argv[0], lstr, e);
            return 1;
        }
    }

    /* バッファサイズのパース */
    {
        char *e;
        const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "buffer-size");
        num_buffer_samples = (uint32_t)strtol(lstr, &e, 10);
        if (*e != '\0') {
            fprintf(stderr, "%s: invalid buffer size. (irregular character found in %s at %s)\n", argv[0], lstr, e);
            return 1;
        }
    }

    /* クオリティのパース */
    {
        char *e;
        const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, "quality");
        quality = (uint32_t)strtol(lstr, &e, 10);
        if (*e != '\0') {
            fprintf(stderr, "%s: invalid quality. (irregular character found in %s at %s)\n", argv[0], lstr, e);
            return 1;
        }
    }

    /* レート変換実行 */
    if (do_rate_convert(input_file, output_file, output_rate, num_buffer_samples, quality) != 0) {
        fprintf(stderr, "%s: failed to rate conversion. \n", argv[0]);
        return 1;
    }

    return 0;
}
