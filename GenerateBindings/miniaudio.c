#define MINIAUDIO_IMPLEMENTATION
#include "./miniaudio/miniaudio.h"

#include <ogg/ogg.h>
#include <opus/opus.h>
#include <opus/opusfile.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "./miniaudio_ex.h"
#include "./miniaudio_libopus.h"
#include "./miniaudio_libvorbis.h"

ma_result ma_ex_decoder__init_data_converter(ma_decoder* pDecoder, const ma_ex_decoder_config* pConfigEx);

MA_API ma_ex_decoder_config ma_ex_decoder_config_init(ma_format format, ma_uint32 channels, ma_uint32 sampleRate) {
    ma_ex_decoder_config config;
    config.baseConfig = ma_decoder_config_init(format, channels, sampleRate);
    config.allowDynamicSampleRate = MA_FALSE;

    return config;
}

MA_API ma_result ma_ex_decoder_init(ma_decoder_read_proc onRead, ma_decoder_seek_proc onSeek, void* pUserData, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
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
    result = ma_ex_decoder__init_data_converter(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

MA_API ma_result ma_ex_decoder_init_file(const char* pFilePath, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
    ma_result result;

    result = ma_decoder_init_file(pFilePath, &pConfig->baseConfig, pDecoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Samething */
    ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
    result = ma_ex_decoder__init_data_converter(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

MA_API ma_result ma_ex_decoder_init_memory(const void* pData, size_t dataSize, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder) {
    ma_result result;

    result = ma_decoder_init_memory(pData, dataSize, &pConfig->baseConfig, pDecoder);
    if (result != MA_SUCCESS) {
        return result;
    }

    /* Samething */
    ma_data_converter_uninit(&pDecoder->converter, &pDecoder->allocationCallbacks);
    result = ma_ex_decoder__init_data_converter(pDecoder, pConfig);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(pDecoder);
        return result;
    }

    return result;
}

// Copy of ma_decoder_config_init_copy with extended config support.
// This might need to be updated if the base function changes upstream.
static ma_result ma_ex_decoder__init_data_converter(ma_decoder* pDecoder, const ma_ex_decoder_config* pConfigEx)
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

/**
 * Libvorbis backend data
 */

static ma_result ma_libvorbis_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_libvorbis_read_pcm_frames((ma_libvorbis*)pDataSource, pFramesOut, frameCount, pFramesRead);
}

static ma_result ma_libvorbis_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    return ma_libvorbis_seek_to_pcm_frame((ma_libvorbis*)pDataSource, frameIndex);
}

static ma_result ma_libvorbis_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_libvorbis_get_data_format((ma_libvorbis*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_libvorbis_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_libvorbis_get_cursor_in_pcm_frames((ma_libvorbis*)pDataSource, pCursor);
}

static ma_result ma_libvorbis_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    return ma_libvorbis_get_length_in_pcm_frames((ma_libvorbis*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_libvorbis_ds_vtable =
{
    ma_libvorbis_ds_read,
    ma_libvorbis_ds_seek,
    ma_libvorbis_ds_get_data_format,
    ma_libvorbis_ds_get_cursor,
    ma_libvorbis_ds_get_length,
    NULL,   /* onSetLooping */
    0       /* flags */
};

static size_t ma_libvorbis_vf_callback__read(void* pBufferOut, size_t size, size_t count, void* pUserData)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pUserData;
    ma_result result;
    size_t bytesToRead;
    size_t bytesRead;

    /* For consistency with fread(). If `size` of `count` is 0, return 0 immediately without changing anything. */
    if (size == 0 || count == 0) {
        return 0;
    }

    bytesToRead = size * count;
    result = pVorbis->onRead(pVorbis->pReadSeekTellUserData, pBufferOut, bytesToRead, &bytesRead);
    if (result != MA_SUCCESS) {
        /* Not entirely sure what to return here. What if an error occurs, but some data was read and bytesRead is > 0? */
        return 0;
    }

    return bytesRead / size;
}

static int ma_libvorbis_vf_callback__seek(void* pUserData, ogg_int64_t offset, int whence)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pUserData;
    ma_result result;
    ma_seek_origin origin;

    if (whence == SEEK_SET) {
        origin = ma_seek_origin_start;
    } else if (whence == SEEK_END) {
        origin = ma_seek_origin_end;
    } else {
        origin = ma_seek_origin_current;
    }

    result = pVorbis->onSeek(pVorbis->pReadSeekTellUserData, offset, origin);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return 0;
}

static long ma_libvorbis_vf_callback__tell(void* pUserData)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pUserData;
    ma_result result;
    ma_int64 cursor;
    
    result = pVorbis->onTell(pVorbis->pReadSeekTellUserData, &cursor);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return (long)cursor;
}

static ma_result ma_libvorbis_init_internal(const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    memset(pVorbis, 0, sizeof(*pVorbis));
    pVorbis->format = ma_format_f32;    /* f32 by default. */

    if (pConfig != NULL && (pConfig->preferredFormat == ma_format_f32 || pConfig->preferredFormat == ma_format_s16)) {
        pVorbis->format = pConfig->preferredFormat;
    } else {
        /* Getting here means something other than f32 and s16 was specified. Just leave this unset to use the default format. */
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        ma_result result;
        ma_data_source_config dataSourceConfig;

        dataSourceConfig = ma_data_source_config_init();
        dataSourceConfig.vtable = &g_ma_libvorbis_ds_vtable;

        result = ma_data_source_init(&dataSourceConfig, &pVorbis->ds);
        if (result != MA_SUCCESS) {
            return result;  /* Failed to initialize the base data source. */
        }

        pVorbis->vf = (OggVorbis_File*)ma_malloc(sizeof(OggVorbis_File), pAllocationCallbacks);
        if (pVorbis->vf == NULL) {
            ma_data_source_uninit(&pVorbis->ds);
            return MA_OUT_OF_MEMORY;
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. */
        (void)pAllocationCallbacks;
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    ma_result result;

    (void)pAllocationCallbacks; /* Can't seem to find a way to configure memory allocations in libvorbis. */

    if (onRead == NULL || onSeek == NULL) {
        return MA_INVALID_ARGS; /* onRead and onSeek are mandatory. */
    }
    
    result = ma_libvorbis_init_internal(pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        return result;
    }

    pVorbis->onRead = onRead;
    pVorbis->onSeek = onSeek;
    pVorbis->onTell = onTell;
    pVorbis->pReadSeekTellUserData = pReadSeekTellUserData;

    #if !defined(MA_NO_LIBVORBIS)
    {
        int libvorbisResult;
        ov_callbacks libvorbisCallbacks;

        /* We can now initialize the vorbis decoder. This must be done after we've set up the callbacks. */
        libvorbisCallbacks.read_func  = ma_libvorbis_vf_callback__read;
        libvorbisCallbacks.seek_func  = ma_libvorbis_vf_callback__seek;
        libvorbisCallbacks.close_func = NULL;
        libvorbisCallbacks.tell_func  = ma_libvorbis_vf_callback__tell;

        libvorbisResult = ov_open_callbacks(pVorbis, (OggVorbis_File*)pVorbis->vf, NULL, 0, libvorbisCallbacks);
        if (libvorbisResult < 0) {
            ma_data_source_uninit(&pVorbis->ds);
            ma_free(pVorbis->vf, pAllocationCallbacks);
            return MA_INVALID_FILE;
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. */
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libvorbis* pVorbis)
{
    ma_result result;

    (void)pAllocationCallbacks; /* Can't seem to find a way to configure memory allocations in libvorbis. */

    result = ma_libvorbis_init_internal(pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        return result;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        int libvorbisResult;

        libvorbisResult = ov_fopen(pFilePath, (OggVorbis_File*)pVorbis->vf);
        if (libvorbisResult < 0) {
            ma_data_source_uninit(&pVorbis->ds);
            ma_free(pVorbis->vf, pAllocationCallbacks);
            return MA_INVALID_FILE;
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. */
        (void)pFilePath;
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API void ma_libvorbis_uninit(ma_libvorbis* pVorbis, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pVorbis == NULL) {
        return;
    }

    (void)pAllocationCallbacks;

    #if !defined(MA_NO_LIBVORBIS)
    {
        ov_clear((OggVorbis_File*)pVorbis->vf);
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
    }
    #endif

    ma_data_source_uninit(&pVorbis->ds);
    ma_free(pVorbis->vf, pAllocationCallbacks);
}

MA_API ma_result ma_libvorbis_read_pcm_frames(ma_libvorbis* pVorbis, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (frameCount == 0) {
        return MA_INVALID_ARGS;
    }

    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        /* We always use floating point format. */
        ma_result result = MA_SUCCESS;  /* Must be initialized to MA_SUCCESS. */
        ma_uint64 totalFramesRead;
        ma_format format;
        ma_uint32 channels;

        ma_libvorbis_get_data_format(pVorbis, &format, &channels, NULL, NULL, 0);

        totalFramesRead = 0;
        while (totalFramesRead < frameCount) {
            long libvorbisResult;
            ma_uint64 framesToRead;
            ma_uint64 framesRemaining;

            framesRemaining = (frameCount - totalFramesRead);
            framesToRead = 1024;
            if (framesToRead > framesRemaining) {
                framesToRead = framesRemaining;
            }

            if (format == ma_format_f32) {
                float** ppFramesF32;

                libvorbisResult = ov_read_float((OggVorbis_File*)pVorbis->vf, &ppFramesF32, (int)framesToRead, NULL);
                if (libvorbisResult < 0) {
                    result = MA_ERROR;  /* Error while decoding. */
                    break;
                } else {
                    /* Frames need to be interleaved. */
                    ma_interleave_pcm_frames(format, channels, libvorbisResult, (const void**)ppFramesF32, ma_offset_pcm_frames_ptr(pFramesOut, totalFramesRead, format, channels));
                    totalFramesRead += libvorbisResult;

                    if (libvorbisResult == 0) {
                        result = MA_AT_END;
                        break;
                    }
                }
            } else {
                libvorbisResult = ov_read((OggVorbis_File*)pVorbis->vf, (char*)ma_offset_pcm_frames_ptr(pFramesOut, totalFramesRead, format, channels), (int)(framesToRead * ma_get_bytes_per_frame(format, channels)), 0, 2, 1, NULL);
                if (libvorbisResult < 0) {
                    result = MA_ERROR;  /* Error while decoding. */
                    break;
                } else {
                    /* Conveniently, there's no need to interleaving when using ov_read(). I'm not sure why ov_read_float() is different in that regard... */
                    totalFramesRead += libvorbisResult / ma_get_bytes_per_frame(format, channels);

                    if (libvorbisResult == 0) {
                        result = MA_AT_END;
                        break;
                    }
                }
            }
        }

        if (pFramesRead != NULL) {
            *pFramesRead = totalFramesRead;
        }

        if (result == MA_SUCCESS && totalFramesRead == 0) {
            result = MA_AT_END;
        }

        return result;
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);

        (void)pFramesOut;
        (void)frameCount;
        (void)pFramesRead;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_seek_to_pcm_frame(ma_libvorbis* pVorbis, ma_uint64 frameIndex)
{
    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        int libvorbisResult = ov_pcm_seek((OggVorbis_File*)pVorbis->vf, (ogg_int64_t)frameIndex);
        if (libvorbisResult != 0) {
            if (libvorbisResult == OV_ENOSEEK) {
                return MA_INVALID_OPERATION;    /* Not seekable. */
            } else if (libvorbisResult == OV_EINVAL) {
                return MA_INVALID_ARGS;
            } else {
                return MA_ERROR;
            }
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);

        (void)frameIndex;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_get_data_format(ma_libvorbis* pVorbis, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    /* Defaults for safety. */
    if (pFormat != NULL) {
        *pFormat = ma_format_unknown;
    }
    if (pChannels != NULL) {
        *pChannels = 0;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = 0;
    }
    if (pChannelMap != NULL) {
        memset(pChannelMap, 0, sizeof(*pChannelMap) * channelMapCap);
    }

    if (pVorbis == NULL) {
        return MA_INVALID_OPERATION;
    }

    if (pFormat != NULL) {
        *pFormat = pVorbis->format;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        vorbis_info* pInfo = ov_info((OggVorbis_File*)pVorbis->vf, 0);
        if (pInfo == NULL) {
            return MA_INVALID_OPERATION;
        }

        if (pChannels != NULL) {
            *pChannels = pInfo->channels;
        }

        if (pSampleRate != NULL) {
            *pSampleRate = pInfo->rate;
        }

        if (pChannelMap != NULL) {
            ma_channel_map_init_standard(ma_standard_channel_map_vorbis, pChannelMap, channelMapCap, pInfo->channels);
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_get_cursor_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pCursor)
{
    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;   /* Safety. */

    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        ogg_int64_t offset = ov_pcm_tell((OggVorbis_File*)pVorbis->vf);
        if (offset < 0) {
            return MA_INVALID_FILE;
        }

        *pCursor = (ma_uint64)offset;

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libvorbis_get_length_in_pcm_frames(ma_libvorbis* pVorbis, ma_uint64* pLength)
{
    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;   /* Safety. */

    if (pVorbis == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBVORBIS)
    {
        /*
        Will work in the supermajority of cases where a file has a single logical bitstream. Concatenated streams
        are much harder to determine the length of since they can have sample rate changes, but they should be
        extremely rare outside of unseekable livestreams anyway.
        */
        if (ov_streams((OggVorbis_File*)pVorbis->vf) == 1) {
            ogg_int64_t length = ov_pcm_total((OggVorbis_File*)pVorbis->vf, 0);
            if(length != OV_EINVAL) {
                *pLength = (ma_uint64)length;
            } else {
                /* Unseekable. */
            }
        } else {
            /* Concatenated stream. */
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libvorbis is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

static ma_result ma_decoding_backend_init__libvorbis(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(*pVorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;

    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libvorbis(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libvorbis* pVorbis;

    (void)pUserData;

    pVorbis = (ma_libvorbis*)ma_malloc(sizeof(*pVorbis), pAllocationCallbacks);
    if (pVorbis == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libvorbis_init_file(pFilePath, pConfig, pAllocationCallbacks, pVorbis);
    if (result != MA_SUCCESS) {
        ma_free(pVorbis, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pVorbis;

    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libvorbis(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    ma_libvorbis* pVorbis = (ma_libvorbis*)pBackend;

    (void)pUserData;

    ma_libvorbis_uninit(pVorbis, pAllocationCallbacks);
    ma_free(pVorbis, pAllocationCallbacks);
}


static ma_decoding_backend_vtable ma_gDecodingBackendVTable_libvorbis =
{
    ma_decoding_backend_init__libvorbis,
    ma_decoding_backend_init_file__libvorbis,
    NULL, /* onInitFileW() */
    NULL, /* onInitMemory() */
    ma_decoding_backend_uninit__libvorbis
};

ma_decoding_backend_vtable* ma_decoding_backend_libvorbis = &ma_gDecodingBackendVTable_libvorbis;

MA_API ma_decoding_backend_vtable* ma_libvorbis_get_decoding_backend_vtable(void)
{
    return &ma_gDecodingBackendVTable_libvorbis;
}

MA_API ma_data_source_vtable* ma_libvorbis_get_data_source_vtable(void)
{
    return &g_ma_libvorbis_ds_vtable;
}

/**
 * Libopus backend data
 */

static ma_result ma_libopus_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_libopus_read_pcm_frames((ma_libopus*)pDataSource, pFramesOut, frameCount, pFramesRead);
}

static ma_result ma_libopus_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    return ma_libopus_seek_to_pcm_frame((ma_libopus*)pDataSource, frameIndex);
}

static ma_result ma_libopus_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_libopus_get_data_format((ma_libopus*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_libopus_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_libopus_get_cursor_in_pcm_frames((ma_libopus*)pDataSource, pCursor);
}

static ma_result ma_libopus_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    return ma_libopus_get_length_in_pcm_frames((ma_libopus*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_libopus_ds_vtable =
{
    ma_libopus_ds_read,
    ma_libopus_ds_seek,
    ma_libopus_ds_get_data_format,
    ma_libopus_ds_get_cursor,
    ma_libopus_ds_get_length,
    NULL,   /* onSetLooping */
    0       /* flags */
};

static int ma_libopus_of_callback__read(void* pUserData, unsigned char* pBufferOut, int bytesToRead)
{
    ma_libopus* pOpus = (ma_libopus*)pUserData;
    ma_result result;
    size_t bytesRead;

    result = pOpus->onRead(pOpus->pReadSeekTellUserData, (void*)pBufferOut, bytesToRead, &bytesRead);

    if (result != MA_SUCCESS) {
        return -1;
    }

    return (int)bytesRead;
}

static int ma_libopus_of_callback__seek(void* pUserData, ogg_int64_t offset, int whence)
{
    ma_libopus* pOpus = (ma_libopus*)pUserData;
    ma_result result;
    ma_seek_origin origin;

    if (whence == SEEK_SET) {
        origin = ma_seek_origin_start;
    } else if (whence == SEEK_END) {
        origin = ma_seek_origin_end;
    } else {
        origin = ma_seek_origin_current;
    }

    result = pOpus->onSeek(pOpus->pReadSeekTellUserData, offset, origin);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return 0;
}

static opus_int64 ma_libopus_of_callback__tell(void* pUserData)
{
    ma_libopus* pOpus = (ma_libopus*)pUserData;
    ma_result result;
    ma_int64 cursor;

    if (pOpus->onTell == NULL) {
        return -1;
    }

    result = pOpus->onTell(pOpus->pReadSeekTellUserData, &cursor);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return cursor;
}

static ma_result ma_libopus_init_internal(const ma_decoding_backend_config* pConfig, ma_libopus* pOpus)
{
    ma_result result;
    ma_data_source_config dataSourceConfig;

    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    memset(pOpus, 0, sizeof(*pOpus));
    pOpus->format = ma_format_f32;    /* f32 by default. */

    if (pConfig != NULL && (pConfig->preferredFormat == ma_format_f32 || pConfig->preferredFormat == ma_format_s16)) {
        pOpus->format = pConfig->preferredFormat;
    } else {
        /* Getting here means something other than f32 and s16 was specified. Just leave this unset to use the default format. */
    }

    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_libopus_ds_vtable;

    result = ma_data_source_init(&dataSourceConfig, &pOpus->ds);
    if (result != MA_SUCCESS) {
        return result;  /* Failed to initialize the base data source. */
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_libopus_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus)
{
    ma_result result;

    (void)pAllocationCallbacks; /* Can't seem to find a way to configure memory allocations in libopus. */
    
    result = ma_libopus_init_internal(pConfig, pOpus);
    if (result != MA_SUCCESS) {
        return result;
    }

    if (onRead == NULL || onSeek == NULL) {
        return MA_INVALID_ARGS; /* onRead and onSeek are mandatory. */
    }

    pOpus->onRead = onRead;
    pOpus->onSeek = onSeek;
    pOpus->onTell = onTell;
    pOpus->pReadSeekTellUserData = pReadSeekTellUserData;

    #if !defined(MA_NO_LIBOPUS)
    {
        int libopusResult;
        OpusFileCallbacks libopusCallbacks;

        /* We can now initialize the Opus decoder. This must be done after we've set up the callbacks. */
        libopusCallbacks.read  = ma_libopus_of_callback__read;
        libopusCallbacks.seek  = ma_libopus_of_callback__seek;
        libopusCallbacks.close = NULL;
        libopusCallbacks.tell  = ma_libopus_of_callback__tell;

        pOpus->of = op_open_callbacks(pOpus, &libopusCallbacks, NULL, 0, &libopusResult);
        if (pOpus->of == NULL) {
            return MA_INVALID_FILE;
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. */
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libopus_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libopus* pOpus)
{
    ma_result result;

    (void)pAllocationCallbacks; /* Can't seem to find a way to configure memory allocations in libopus. */

    result = ma_libopus_init_internal(pConfig, pOpus);
    if (result != MA_SUCCESS) {
        return result;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        int libopusResult;

        pOpus->of = op_open_file(pFilePath, &libopusResult);
        if (pOpus->of == NULL) {
            return MA_INVALID_FILE;
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. */
        (void)pFilePath;
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API void ma_libopus_uninit(ma_libopus* pOpus, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pOpus == NULL) {
        return;
    }

    (void)pAllocationCallbacks;

    #if !defined(MA_NO_LIBOPUS)
    {
        op_free((OggOpusFile*)pOpus->of);
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
    }
    #endif

    ma_data_source_uninit(&pOpus->ds);
}

MA_API ma_result ma_libopus_read_pcm_frames(ma_libopus* pOpus, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (frameCount == 0) {
        return MA_INVALID_ARGS;
    }

    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        /* We always use floating point format. */
        ma_result result = MA_SUCCESS;  /* Must be initialized to MA_SUCCESS. */
        ma_uint64 totalFramesRead;
        ma_format format;
        ma_uint32 channels;

        ma_libopus_get_data_format(pOpus, &format, &channels, NULL, NULL, 0);

        totalFramesRead = 0;
        while (totalFramesRead < frameCount) {
            long libopusResult;
            ma_uint64 framesToRead;
            ma_uint64 framesRemaining;

            framesRemaining = (frameCount - totalFramesRead);
            framesToRead = 1024;
            if (framesToRead > framesRemaining) {
                framesToRead = framesRemaining;
            }

            if (format == ma_format_f32) {
                libopusResult = op_read_float((OggOpusFile*)pOpus->of, (float*     )ma_offset_pcm_frames_ptr(pFramesOut, totalFramesRead, format, channels), (int)(framesToRead * channels), NULL);
            } else {
                libopusResult = op_read      ((OggOpusFile*)pOpus->of, (opus_int16*)ma_offset_pcm_frames_ptr(pFramesOut, totalFramesRead, format, channels), (int)(framesToRead * channels), NULL);
            }

            if (libopusResult < 0) {
                result = MA_ERROR;  /* Error while decoding. */
                break;
            } else {
                totalFramesRead += libopusResult;

                if (libopusResult == 0) {
                    result = MA_AT_END;
                    break;
                }
            }
        }

        if (pFramesRead != NULL) {
            *pFramesRead = totalFramesRead;
        }

        if (result == MA_SUCCESS && totalFramesRead == 0) {
            result = MA_AT_END;
        }

        return result;
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);

        (void)pFramesOut;
        (void)frameCount;
        (void)pFramesRead;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libopus_seek_to_pcm_frame(ma_libopus* pOpus, ma_uint64 frameIndex)
{
    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        int libopusResult = op_pcm_seek((OggOpusFile*)pOpus->of, (ogg_int64_t)frameIndex);
        if (libopusResult != 0) {
            if (libopusResult == OP_ENOSEEK) {
                return MA_INVALID_OPERATION;    /* Not seekable. */
            } else if (libopusResult == OP_EINVAL) {
                return MA_INVALID_ARGS;
            } else {
                return MA_ERROR;
            }
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);

        (void)frameIndex;

        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libopus_get_data_format(ma_libopus* pOpus, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    /* Defaults for safety. */
    if (pFormat != NULL) {
        *pFormat = ma_format_unknown;
    }
    if (pChannels != NULL) {
        *pChannels = 0;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = 0;
    }
    if (pChannelMap != NULL) {
        memset(pChannelMap, 0, sizeof(*pChannelMap) * channelMapCap);
    }

    if (pOpus == NULL) {
        return MA_INVALID_OPERATION;
    }

    if (pFormat != NULL) {
        *pFormat = pOpus->format;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        ma_uint32 channels = op_channel_count((OggOpusFile*)pOpus->of, -1);

        if (pChannels != NULL) {
            *pChannels = channels;
        }

        if (pSampleRate != NULL) {
            *pSampleRate = 48000;
        }

        if (pChannelMap != NULL) {
            ma_channel_map_init_standard(ma_standard_channel_map_vorbis, pChannelMap, channelMapCap, channels);
        }

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libopus_get_cursor_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pCursor)
{
    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;   /* Safety. */

    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        ogg_int64_t offset = op_pcm_tell((OggOpusFile*)pOpus->of);
        if (offset < 0) {
            return MA_INVALID_FILE;
        }

        *pCursor = (ma_uint64)offset;

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

MA_API ma_result ma_libopus_get_length_in_pcm_frames(ma_libopus* pOpus, ma_uint64* pLength)
{
    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;   /* Safety. */

    if (pOpus == NULL) {
        return MA_INVALID_ARGS;
    }

    #if !defined(MA_NO_LIBOPUS)
    {
        ogg_int64_t length = op_pcm_total((OggOpusFile*)pOpus->of, -1);
        if (length < 0) {
            return MA_ERROR;
        }

        *pLength = (ma_uint64)length;

        return MA_SUCCESS;
    }
    #else
    {
        /* libopus is disabled. Should never hit this since initialization would have failed. */
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
    #endif
}

static ma_result ma_decoding_backend_init__libopus(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libopus* pOpus;

    (void)pUserData;

    pOpus = (ma_libopus*)ma_malloc(sizeof(*pOpus), pAllocationCallbacks);
    if (pOpus == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libopus_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pOpus);
    if (result != MA_SUCCESS) {
        ma_free(pOpus, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pOpus;

    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libopus(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libopus* pOpus;

    (void)pUserData;

    pOpus = (ma_libopus*)ma_malloc(sizeof(*pOpus), pAllocationCallbacks);
    if (pOpus == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libopus_init_file(pFilePath, pConfig, pAllocationCallbacks, pOpus);
    if (result != MA_SUCCESS) {
        ma_free(pOpus, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pOpus;

    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libopus(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    ma_libopus* pOpus = (ma_libopus*)pBackend;

    (void)pUserData;

    ma_libopus_uninit(pOpus, pAllocationCallbacks);
    ma_free(pOpus, pAllocationCallbacks);
}

static ma_decoding_backend_vtable ma_gDecodingBackendVTable_libopus =
{
    ma_decoding_backend_init__libopus,
    ma_decoding_backend_init_file__libopus,
    NULL, /* onInitFileW() */
    NULL, /* onInitMemory() */
    ma_decoding_backend_uninit__libopus
};
ma_decoding_backend_vtable* ma_decoding_backend_libopus = &ma_gDecodingBackendVTable_libopus;