using System;
using UnityEngine;

namespace Tide
{
    // The game's sound in Unity: Tide's mixer (tide/embed.h's tide_embed_audio), which an AudioSource of its own
    // reads on Unity's audio thread (OnAudioFilterRead), so the runner's audioOutput mixer group, Unity's volume and
    // its mute apply. And Unity's microphone, while the game listens to it (Clip.Microphone), handed to Tide each
    // frame. The runner makes it on a GameObject under its own, "Tide Audio". On the web Unity has no
    // OnAudioFilterRead and no microphone: Tide's program beside Unity's plays through the page and hears the page's
    // microphone itself, and this isn't made.
    //
    // Nothing is heard and no microphone opens in a run nobody watches (TIDE_HIDDEN, as Tide's own window keeps
    // quiet): the mixer is still read, which tests look at (Loudest).
    [RequireComponent(typeof(AudioSource))]
    public sealed unsafe class TideAudio : MonoBehaviour
    {
        // What the audio thread reads through: set and cleared under `gate`, so the embed is never destroyed while
        // that thread is inside it
        readonly object gate = new object();
        TideLibrary library;
        IntPtr embed;
        int rate;
        static readonly bool hidden = Hidden();

        float loudest;
        int reads;

        // The loudest sample the game played since this was last asked, 0 to 1 and more (a level meter's), and the
        // times Unity's audio thread asked for samples since this started
        public float TakeLoudest()
        {
            lock (gate)
            {
                var most = loudest;
                loudest = 0f;
                return most;
            }
        }

        public int Reads => reads;

        internal void Attach(TideLibrary to, IntPtr running, UnityEngine.Audio.AudioMixerGroup output)
        {
            rate = AudioSettings.outputSampleRate;
            lock (gate)
            {
                library = to;
                embed = running;
            }
            var source = GetComponent<AudioSource>();
            if (source.clip == null)
            {
                // Silence that loops, under what the filter writes: a source that plays nothing calls no filter
                source.clip = AudioClip.Create("Tide", Math.Max(1, rate), 1, Math.Max(1, rate), false);
            }
            source.outputAudioMixerGroup = output;
            source.loop = true;
            source.playOnAwake = false;
            source.spatialBlend = 0f;
            source.bypassEffects = false;
            Update();
            AudioSettings.OnAudioConfigurationChanged += ConfigurationChanged;
        }

        // The runner's embed goes: the audio thread reads it no more, and Tide's told that thread's done.
        internal void Detach()
        {
            AudioSettings.OnAudioConfigurationChanged -= ConfigurationChanged;
            TideLibrary had;
            lock (gate)
            {
                had = library;
                library = null;
                embed = IntPtr.Zero;
            }
            had?.Audio(IntPtr.Zero, null, 0, 0, 0);
            StopMicrophone();
            playing = false;
            if (this != null && TryGetComponent(out AudioSource source)) source.Stop();
        }

        // With no AudioListener nobody hears it, and Unity says so in the Console each block a source plays: the source
        // plays while one listens, looked for each frame
        bool playing; // As this said: a source's isPlaying is false a while after Play

        void Update()
        {
            var heard = library != null && FindAnyObjectByType<AudioListener>() != null; // And while there's a game
            if (heard == playing) return;
            playing = heard;
            var source = GetComponent<AudioSource>();
            if (heard) source.Play();
            else source.Stop();
        }

        void ConfigurationChanged(bool deviceWasChanged)
        {
            rate = AudioSettings.outputSampleRate;
        }

        void OnDestroy()
        {
            Detach();
        }

        // Unity's audio thread: the block Unity's about to play, which is the mixer's
        void OnAudioFilterRead(float[] data, int channels)
        {
            lock (gate)
            {
                if (library == null || channels < 1 || channels > 8) return;
                fixed (float* samples = data) library.Audio(embed, samples, (uint)(data.Length / channels), (uint)rate, (uint)channels);
                reads++;
                var most = loudest;
                for (var i = 0; i < data.Length; i++)
                {
                    var a = Math.Abs(data[i]);
                    if (a > most) most = a;
                }
                loudest = most;
            }
            if (hidden) Array.Clear(data, 0, data.Length);
        }

#if !UNITY_WEBGL || UNITY_EDITOR
        AudioClip microphone;
        int microphoneAt;
        float[] heard = Array.Empty<float>();
        float askedAt = -1f;

        // The frame's: Unity's microphone while the game listens (the status says so), what it heard since the last
        // frame handed over.
        internal void Listen(bool listening)
        {
            if (!listening || hidden)
            {
                StopMicrophone();
                return;
            }
            if (microphone == null)
            {
                if (Time.unscaledTime - askedAt < 1f) return; // A permission being asked, or no device: once a second
                askedAt = Time.unscaledTime;
                if (!Permitted() || Microphone.devices.Length == 0) return;
                microphone = Microphone.Start(null, true, 1, 48000);
                microphoneAt = 0;
                if (microphone == null) return;
            }
            var at = Microphone.GetPosition(null);
            var length = microphone.samples;
            var count = (at - microphoneAt + length) % length;
            if (count == 0) return;
            var wanted = count * microphone.channels;
            if (heard.Length != wanted) heard = new float[wanted];
            microphone.GetData(heard, microphoneAt); // Around the end of the clip's ring to its start, where it goes on
            microphoneAt = at;
            lock (gate)
            {
                if (library == null) return;
                fixed (float* samples = heard) library.Microphone(embed, samples, (uint)count, (uint)microphone.frequency, (uint)microphone.channels);
            }
        }

        void StopMicrophone()
        {
            if (microphone == null) return;
            Microphone.End(null);
            Destroy(microphone);
            microphone = null;
        }

        // A phone's player allowed the microphone; asked when not (yet)
        static bool Permitted()
        {
#if UNITY_ANDROID && !UNITY_EDITOR
            if (UnityEngine.Android.Permission.HasUserAuthorizedPermission(UnityEngine.Android.Permission.Microphone)) return true;
            UnityEngine.Android.Permission.RequestUserPermission(UnityEngine.Android.Permission.Microphone);
            return false;
#elif UNITY_IOS && !UNITY_EDITOR
            if (Application.HasUserAuthorization(UserAuthorization.Microphone)) return true;
            Application.RequestUserAuthorization(UserAuthorization.Microphone);
            return false;
#else
            return true;
#endif
        }
#else
        internal void Listen(bool listening)
        {
        }

        void StopMicrophone()
        {
        }
#endif

        static bool Hidden()
        {
            var value = Environment.GetEnvironmentVariable("TIDE_HIDDEN");
            return !string.IsNullOrEmpty(value) && value != "0";
        }
    }
}
