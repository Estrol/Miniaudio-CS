#ifndef MINIAUDIO_EX_H
#define MINIAUDIO_EX_H
#ifdef __cplusplus
extern "C" {
#endif


#include "./miniaudio/miniaudio.h"

#if defined(MA_DLL)
    #if defined(_WIN32)
        #define MA_DLL_IMPORT  __declspec(dllimport)
        #define MA_DLL_EXPORT  __declspec(dllexport)
        #define MA_DLL_PRIVATE static
    #else
        #if defined(__GNUC__) && __GNUC__ >= 4
            #define MA_DLL_IMPORT  __attribute__((visibility("default")))
            #define MA_DLL_EXPORT  __attribute__((visibility("default")))
            #define MA_DLL_PRIVATE __attribute__((visibility("hidden")))
        #else
            #define MA_DLL_IMPORT
            #define MA_DLL_EXPORT
            #define MA_DLL_PRIVATE static
        #endif
    #endif
#endif

#if !defined(MA_EX_API)
    #if defined(MA_DLL)
        #if defined(MINIAUDIO_VORBIS_IMPLEMENTATION) || defined(MA_VORBIS_IMPLEMENTATION)
            #define MA_EX_API  MA_DLL_EXPORT
        #else
            #define MA_EX_API  MA_DLL_IMPORT
        #endif
    #else
        #define MA_EX_API extern
    #endif
#endif

typedef struct
{
    ma_decoder_config baseConfig;
    ma_bool32 allowDynamicSampleRate; /* When set to true, allows the sample rate to change dynamically. */
} ma_ex_decoder_config;

MA_EX_API ma_ex_decoder_config ma_ex_decoder_config_init(ma_format format, ma_uint32 channels, ma_uint32 sampleRate);
MA_EX_API ma_result ma_ex_decoder_init(ma_decoder_read_proc onRead, ma_decoder_seek_proc onSeek, void* pUserData, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);
MA_EX_API ma_result ma_ex_decoder_init_file(const char* pFilePath, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);
MA_EX_API ma_result ma_ex_decoder_init_memory(const void* pData, size_t dataSize, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);

#ifdef __cplusplus
}
#endif

#endif  /* MINIAUDIO_EX_H */