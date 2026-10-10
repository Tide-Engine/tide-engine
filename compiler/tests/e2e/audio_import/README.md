# Audio import fixtures

These are synthetic signals authored for Tide's tests, encoded with FFmpeg
8.1.1 (`libmp3lame` and `flac`). No recorded or third-party audio is included.
The source and fixtures follow the repository's license. MP3 references are
FFmpeg's independent float32 decodes; comparisons allow 0.0002 sample error.
Both MP3s contain encoder delay/padding metadata and must import to exactly
0.2 seconds: 8820 stereo frames at 44100 Hz and 4410 mono frames at 22050 Hz.

`precise24.flac` is 4096 stereo frames at 16000 Hz. Its independently specified
24-bit integer samples are:

```text
left(i)  = ((i * 73) % 4096 - 2048) * 256 + 17
right(i) = ((i * 131) % 2048 - 1024) * 1024 - 31
```

The low bits deliberately survive only when import preserves more than
16-bit precision. Unit and native/web pack tests check every sample exactly.

Reproduce the valid fixtures from the repository root with PowerShell and
FFmpeg (output names assume the fixture directory already exists):

```powershell
$raw = New-Object byte[] (4096 * 6)
for ($i = 0; $i -lt 4096; $i++) {
    $left = (($i * 73) % 4096 - 2048) * 256 + 17
    $right = (($i * 131) % 2048 - 1024) * 1024 - 31
    for ($b = 0; $b -lt 3; $b++) {
        $raw[$i * 6 + $b] = ($left -shr ($b * 8)) -band 255
        $raw[$i * 6 + 3 + $b] = ($right -shr ($b * 8)) -band 255
    }
}
[IO.File]::WriteAllBytes("$PWD/build/audio_import_source.s24", $raw)
ffmpeg -y -f s24le -ar 16000 -ac 2 -i build/audio_import_source.s24 -c:a flac -sample_fmt s32 compiler/tests/e2e/audio_import/precise24.flac
ffmpeg -y -f lavfi -i 'sine=frequency=733:sample_rate=44100:duration=0.2' -ac 2 -c:a libmp3lame -b:a 128k compiler/tests/e2e/audio_import/stereo_cbr.mp3
ffmpeg -y -f lavfi -i 'sine=frequency=487:sample_rate=22050:duration=0.2' -ac 1 -c:a libmp3lame -q:a 4 compiler/tests/e2e/audio_import/mono_vbr.mp3
ffmpeg -y -i compiler/tests/e2e/audio_import/stereo_cbr.mp3 -f f32le -acodec pcm_f32le compiler/tests/e2e/audio_import/stereo_cbr.f32
ffmpeg -y -i compiler/tests/e2e/audio_import/mono_vbr.mp3 -f f32le -acodec pcm_f32le compiler/tests/e2e/audio_import/mono_vbr.f32
```

The `bad/truncated.*` fixtures remove exactly the last byte of their valid
counterparts. `bad/adpcm.wav` is a 48-byte ordinary RIFF/WAVE with format tag
0x0011 (IMA ADPCM), one channel at 48000 Hz, four data bytes. Its format tag
must produce a specific unsupported-codec diagnostic. `bad/unsupported.opus`
is a minimal 36-byte Ogg identification prefix with one lacing value of 8
and the `OpusHead` signature; it tests format identification, not Opus decode.
