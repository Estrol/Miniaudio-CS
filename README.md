# Miniaudio-CS

Auto-generated C# bindings for [miniaudio](https://github.com/mackron/miniaudio)

Miniaudio version: 0.11.23

Fork feature: Ogg vorbis and Opus decoding support via [opusfile](https://github.com/xiph/opusfile) and [libvorbis](https://xiph.org/vorbis/)

## Example
```cs
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using Miniaudio;

internal class Program
{
    static readonly unsafe ma_decoder* decoder;

    public unsafe static void Main(string[] args)
    {
        ma_device* device = (ma_device*)NativeMemory.Alloc((nuint)sizeof(ma_device));
        ma_device_config config = ma.device_config_init(ma_device_type.Playback);
        config.playback.format = ma_format.ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = 44100;
        config.dataCallback = &DataCallback;
        config.pUserData = null;

        ma.device_init(null, &config, device);
        ma_decoder_config decoderConfig = ma.decoder_config_init(ma_format.ma_format_f32, 2, 44100);
        decoder = (ma_decoder*)NativeMemory.Alloc((nuint)sizeof(ma_decoder));

        fixed (byte* p = Encoding.ASCII.GetBytes("music.mp3"))
        {
            ma.decoder_init_file((sbyte*)p, &decoderConfig, decoder);
        }

        ma.device_start(device);

        Console.ReadKey();
        
        ma.device_uninit(device);
        NativeMemory.Free(device);

        ma.decoder_uninit(decoder);
        NativeMemory.Free(decoder);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe void DataCallback(ma_device* device, void* output, void* input, uint frameCount)
    {
        if (decoder == null)
        {
            return;
        }

        ulong framesRead = 0;
        ma_result result = ma.decoder_read_pcm_frames(decoder, output, frameCount, &framesRead);
        if (result != ma_result.MA_SUCCESS || framesRead == 0)
        {
            // Close the device
            ma.device_stop(device);
        }
    }
}

```


```c#
using System.Runtime.InteropServices;
using Miniaudio;

internal class Program
{
    public unsafe static void Main(string[] args)
    {
        // It is recommended to allocate native memory for miniaudio structures.
        ma_engine* engine = (ma_engine*)NativeMemory.Alloc((nuint)sizeof(ma_engine));
        ma.engine_init(null, engine);

        // When calling a function that accepts a string of type "sbyte*",
        // always convert the string to ANSI encoding first.
        string filePath1 = "music.mp3";
        ma_sound* sound1 = (ma_sound*)NativeMemory.Alloc((nuint)sizeof(ma_sound));
        nint filePath1_Ansi = Marshal.StringToHGlobalAnsi(filePath1);
        ma.sound_init_from_file(engine, (sbyte*)filePath1_Ansi, 0, null, null, sound1);
        Marshal.FreeHGlobal(filePath1_Ansi);
        ma.sound_start(sound1);

        // Note that ANSI encoding does not support certain characters.
        // It is more recommended to use the following methods.

        Console.ReadKey();
        ma.sound_stop(sound1);

        string filePath2 = "😄.wav";
        ma_sound* sound2 = (ma_sound*)NativeMemory.Alloc((nuint)sizeof(ma_sound));
        fixed (void* p = filePath2) // or Marshal.StringToHGlobalUni(filePath2);
        {
            ma.sound_init_from_file_w(engine, (ushort*)p, (uint)ma_sound_flags.MA_SOUND_FLAG_LOOPING, null, null, sound2);
        }
        ma.sound_start(sound2);

        Console.ReadKey();
        ma.sound_stop(sound2);

        // Clean up
        ma.engine_uninit(engine);
        NativeMemory.Free(engine);
        NativeMemory.Free(sound1);
        NativeMemory.Free(sound2);
    }
}
```



## Generate Bindings (Miniaudio.cs)

```shell
dotnet tool install --global ClangSharpPInvokeGenerator
git clone https://github.com/Estrol/Miniaudio-CS --recursive
cd Miniaudio-CS/GenerateBindings
ClangSharpPInvokeGenerator @generate.gen
```

## Build Native Library

[actions](https://github.com/Estrol/Miniaudio-CS/actions)

## License
