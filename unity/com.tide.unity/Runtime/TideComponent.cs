using System;
using System.Collections.Generic;
using UnityEngine;

namespace Tide
{
    // A Tide component on a GameObject. The package generates one for each of the game's components, into
    // Packages/com.tide.game, its fields the component's. Outside Play they're what's authored, which tide bakes into
    // the game's prefabs and scenes. In Play they're the entity's own values this frame, as Tide's views see them,
    // written before scripts' Update: a script reads them as fields. Writing one changes nothing in the match, which
    // is the server's: send a command.
    [RequireComponent(typeof(TideEntity))]
    public abstract class TideComponent : MonoBehaviour
    {
        // Its Tide component's name, as the schema has it: "Combat.Health"
        public abstract string TideName { get; }

        // The entity's value this frame: `values` its bytes in the frame's view packet, laid out as the schema says
        protected internal abstract unsafe void ReadLive(byte* values, TidePacket packet, TideRunner runner);

        internal int LiveFrame = -1; // The runner's frame that last wrote it: one the entity lost isn't
    }

    // The generated components, by their Tide names, and the schema they were generated from: the runner writes live
    // values only into components of the build it runs.
    public static class TideComponents
    {
        static readonly Dictionary<string, Type> byName = new Dictionary<string, Type>();

        public static ulong Hash { get; private set; }

        public static void Register(string hash, string name, Type type)
        {
            Hash = ulong.TryParse(hash, System.Globalization.NumberStyles.HexNumber, null, out var h) ? h : 0;
            byName[name] = type;
        }

        public static Type Of(string name)
        {
            return name != null && byName.TryGetValue(name, out var type) ? type : null;
        }
    }
}
