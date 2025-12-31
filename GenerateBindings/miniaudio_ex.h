#ifndef MINIAUDIO_EX_H
#define MINIAUDIO_EX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "./miniaudio/miniaudio.h"

typedef struct
{
    ma_decoder_config baseConfig;
    ma_bool32 allowDynamicSampleRate; /* When set to true, allows the sample rate to change dynamically. */
} ma_ex_decoder_config;

MA_API ma_ex_decoder_config ma_ex_decoder_config_init(ma_format format, ma_uint32 channels, ma_uint32 sampleRate);
MA_API ma_result ma_ex_decoder_init(ma_decoder_read_proc onRead, ma_decoder_seek_proc onSeek, void* pUserData, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);
MA_API ma_result ma_ex_decoder_init_file(const char* pFilePath, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);
MA_API ma_result ma_ex_decoder_init_memory(const void* pData, size_t dataSize, const ma_ex_decoder_config* pConfig, ma_decoder* pDecoder);

#ifdef __cplusplus
}
#endif

#endif  /* MINIAUDIO_EX_H */