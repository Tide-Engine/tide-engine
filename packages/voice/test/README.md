# Voice fixtures

`speech.wav` is the first 2.5 seconds of synthetic English speech generated
without an audio device using Windows `System.Speech.Synthesis.SpeechSynthesizer`,
`SetOutputToWaveFile` at 48 kHz, mono, PCM16. The text starts "We are testing
voice chat in a multiplayer game. Meet me at the bridge". It contains no person's
recorded voice. The test embeds its bytes, sends it through the real capture,
RNNoise, Opus and session path, and checks speech survives suppression.

The noise/toggle test uses deterministic white noise generated in C. It checks
attenuation, bypass, stopped/restarted capture and stale filter history without
pinning model output samples across architectures. The host test uses a generated
440 Hz microphone WAV and enables suppression through the Tide field, exercising
the standard host and mixer in three headless processes.
