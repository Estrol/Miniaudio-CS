cmake_minimum_required(VERSION 3.10)
project(miniaudio-native)

find_package(Ogg CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)

add_compile_definitions(MA_DLL)
add_library(miniaudio SHARED ./miniaudio.c)
target_link_libraries(miniaudio PRIVATE Ogg::ogg Vorbis::vorbis Vorbis::vorbisfile Opus::opus OpusFile::opusfile)

if (MSVC)
    target_compile_definitions(miniaudio PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(miniaudio PRIVATE /wd4244 /wd4018 /wd4217)
endif()