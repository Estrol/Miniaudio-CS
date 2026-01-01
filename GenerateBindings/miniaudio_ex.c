#define MINIAUDIO_EX_IMPLEMENTATION
#include "./miniaudio_ex.h"

#ifndef MA_ASSERT
    #include <assert.h>
    #define MA_ASSERT(x) assert(x)
#endif

#ifndef MA_ZERO_OBJECT
    #define MA_ZERO_OBJECT(p) memset((p), 0, sizeof(*(p)))
#endif

#ifndef ma_countof
    #define ma_countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef MA_DATA_CONVERTER_STACK_BUFFER_SIZE
    #define MA_DATA_CONVERTER_STACK_BUFFER_SIZE  4096
#endif

static ma_result ma_decoder__init_data_converter_ex(ma_decoder* pDecoder, const ma_ex_decoder_config* pConfigEx);

MA_EX_API ma_ex_decoder_config ma_ex_decoder_config_init(ma_format format, ma_uint32 channels, ma_uint32 sampleRate) {
    ma_ex_decoder_config config;
    config.baseConfig = ma_decoder_config_init(format, channels, sampleRate);
    config.allowDynamicSampleRate = MA_FALSE;

    return config;
}

MA_EX_API ma_result ma_ex_decoder_init(ma_decoder_read_proc onRead, ma_decoder_seek_proc onSeek, void* pUserData, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
    ma_result result;

    result = ma_decoder_init(onRead, onSeek, pUserData, &pConfig->baseConfig, pDecoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    /*
        HACK: Uninit any existing data converter
        and re-init with extended config support
        since we don't want to modify the original
        ma_decoder_init function signature to allow
        upstream compatibility.
    */
    ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
    result = ma_decoder__init_data_converter_ex(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

MA_EX_API ma_result ma_ex_decoder_init_file(const char* pFilePath, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
    ma_result result;

    result = ma_decoder_init_file(pFilePath, &pConfig->baseConfig, pDecoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Samething */
    ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
    result = ma_decoder__init_data_converter_ex(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

MA_EX_API ma_result ma_ex_decoder_init_memory(const void* pData, size_t dataSize, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
    ma_result result;

    result = ma_decoder_init_memory(pData, dataSize, &pConfig->baseConfig, pDecoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Samething */
    ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
    result = ma_decoder__init_data_converter_ex(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

// Copy of ma_decoder_config_init_copy with extended config support.
// This might need to be updated if the base function changes upstream.
static ma_result ma_decoder__init_data_converter_ex(ma_decoder* pDecoder, const ma_ex_decoder_config* pConfigEx)
{
    ma_result result;
    ma_data_converter_config converterConfig;
    ma_format internalFormat;
    ma_uint32 internalChannels;
    ma_uint32 internalSampleRate;
    ma_channel internalChannelMap[MA_MAX_CHANNELS];

    MA_ASSERT(pDecoder != NULL);
    MA_ASSERT(pConfigEx  != NULL);

    result = ma_data_source_get_data_format(pDecoder->pBackend, &internalFormat, &internalChannels, &internalSampleRate, internalChannelMap, ma_countof(internalChannelMap));
    if (result != MA_SUCCESS) {
        return result;  /* Failed to retrieve the internal data format. */
    }

    const ma_decoder_config* pConfig = &pConfigEx->baseConfig;

    /* Make sure we're not asking for too many channels. */
    if (pConfig->channels > MA_MAX_CHANNELS) {
        return MA_INVALID_ARGS;
    }

    /* The internal channels should have already been validated at a higher level, but we'll do it again explicitly here for safety. */
    if (internalChannels > MA_MAX_CHANNELS) {
        return MA_INVALID_ARGS;
    }

    /* Output format. */
    if (pConfig->format == ma_format_unknown) {
        pDecoder->outputFormat = internalFormat;
    } else {
        pDecoder->outputFormat = pConfig->format;
    }

    if (pConfig->channels == 0) {
        pDecoder->outputChannels = internalChannels;
    } else {
        pDecoder->outputChannels = pConfig->channels;
    }

    if (pConfig->sampleRate == 0) {
        pDecoder->outputSampleRate = internalSampleRate;
    } else {
        pDecoder->outputSampleRate = pConfig->sampleRate;
    }

    converterConfig = ma_data_converter_config_init(
        internalFormat,     pDecoder->outputFormat,
        internalChannels,   pDecoder->outputChannels,
        internalSampleRate, pDecoder->outputSampleRate
    );
    converterConfig.pChannelMapIn          = internalChannelMap;
    converterConfig.pChannelMapOut         = pConfig->pChannelMap;
    converterConfig.channelMixMode         = pConfig->channelMixMode;
    converterConfig.ditherMode             = pConfig->ditherMode;
    converterConfig.allowDynamicSampleRate = pConfigEx->allowDynamicSampleRate; /* Setting this to true will disable passthrough optimizations. */
    converterConfig.resampling             = pConfig->resampling;

    result = ma_data_converter_init(&converterConfig, &pDecoder->allocationCallbacks, &pDecoder->converter);
    if (result != MA_SUCCESS) {
        return result;
    }

    /*
    Now that we have the decoder we need to determine whether or not we need a heap-allocated cache. We'll
    need this if the data converter does not support calculation of the required input frame count. To
    determine support for this we'll just run a test.
    */
    {
        ma_uint64 unused;

        result = ma_data_converter_get_required_input_frame_count(&pDecoder->converter, 1, &unused);
        if (result != MA_SUCCESS) {
            /*
            We were unable to calculate the required input frame count which means we'll need to use
            a heap-allocated cache.
            */
            ma_uint64 inputCacheCapSizeInBytes;

            pDecoder->inputCacheCap = MA_DATA_CONVERTER_STACK_BUFFER_SIZE / ma_get_bytes_per_frame(internalFormat, internalChannels);

            /* Not strictly necessary, but keeping here for safety in case we change the default value of pDecoder->inputCacheCap. */
            inputCacheCapSizeInBytes = pDecoder->inputCacheCap * ma_get_bytes_per_frame(internalFormat, internalChannels);
            if (inputCacheCapSizeInBytes > MA_SIZE_MAX) {
                ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
                return MA_OUT_OF_MEMORY;
            }

            /* 
            Since we did HACK ways, we need to deallocate the pInputCache as well
            since it might have been allocated with different size before
            */
            if (pDecoder->pInputCache != NULL) {
                ma_free(pDecoder->pInputCache, &pDecoder->allocationCallbacks);
                pDecoder->pInputCache = NULL;
            }

            pDecoder->pInputCache = ma_malloc((size_t)inputCacheCapSizeInBytes, &pDecoder->allocationCallbacks);    /* Safe cast to size_t. */
            if (pDecoder->pInputCache == NULL) {
                ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
                return MA_OUT_OF_MEMORY;
            }
        }
    }

    return MA_SUCCESS;
}