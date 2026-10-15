# Inside Out audio

These are original synthesized sounds. `sound.tide` routes music, game
effects and interface cues through separate buses. The maximum bus gains
are 0.12 for music and 0.3 for effects/interface; source files are already
balanced. Rooms' master volume and the mute setting apply to every bus.

The 84-second game loop and 36-second menu loop are stereo 48 kHz Vorbis.
They crossfade over one second when entering/leaving play. Opening the
in-game menu keeps the game music. Effects are mono 48 kHz PCM16 except
`solve.ogg`, a 2.6-second Vorbis chime. All 29 shipping WAVs have exact zero
endpoints; the reviewed set totals 2,129,691 bytes including the music.
The old ventilation hum and old cues are removed.

`generate/make_music.py` and `generate/make_sfx.py` are the reviewed seeded
generators. They need Python, NumPy and FFmpeg with libvorbis on `PATH`.
Python 3.14, NumPy 2.5 and FFmpeg's bundled libvorbis produced the checked-in
files. PCM generation is reproducible on the same toolchain; compressed
bytes can vary with FFmpeg/libvorbis versions. Neither script plays audio.
They also write large WAV masters and spectrograms, so render into ignored
scratch directories rather than beside the shipping files:

```powershell
$env:TIDE_HIDDEN = '1'
New-Item -ItemType Directory -Force build/audio-repro/music, build/audio-repro/sfx
Copy-Item prototypes/inside-out/sfx/generate/make_music.py build/audio-repro/music/
Copy-Item prototypes/inside-out/sfx/generate/make_sfx.py build/audio-repro/sfx/
python build/audio-repro/music/make_music.py
python build/audio-repro/sfx/make_sfx.py
```

Run `python prototypes/inside-out/sfx/generate/check_assets.py` to check all
shipping hashes, WAV formats, zero endpoints and peak bounds. Its optional
directory argument checks a complete regenerated set instead. Copy the two
regenerated music Oggs into the regenerated SFX directory for that check.
`SHA256SUMS` pins the final reviewed revision, including music revised after
the first asset integration attempt.

The generators measure spectra and loop seams without a listening device.
Their reports and spectrograms are useful engineering checks; the owner
still needs to judge the sound's character by ear.
