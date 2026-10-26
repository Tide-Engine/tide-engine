using System;
using System.Collections.Generic;
using UnityEngine;

namespace Tide
{
    // The game's schema (tide_embed_schema): its types as the game's C lays
    // them out, which the view packet's sections and commands go by.
    [Serializable]
    public sealed class TideSchema
    {
        public uint abi;
        public string hash;   // 16 hex digits: what every view packet of this game says
        public string game;
        public int tickRate;
        public TideSchemaType[] types = Array.Empty<TideSchemaType>();
        public TideSchemaEnum[] enums = Array.Empty<TideSchemaEnum>();
        public int[] components = Array.Empty<int>(); // Match components, by type index
        public int[] singletons = Array.Empty<int>(); // Match singletons, by type index
        public TideSchemaCommand[] commands = Array.Empty<TideSchemaCommand>();
        public string[] prefabs = Array.Empty<string>();
        public string[] scenes = Array.Empty<string>();

        [NonSerialized] Dictionary<string, int> byName;
        [NonSerialized] ulong parsedHash;

        public ulong Hash => parsedHash;

        public static TideSchema Parse(string json)
        {
            var schema = JsonUtility.FromJson<TideSchema>(json);
            if (schema == null) return null;
            schema.parsedHash = ulong.TryParse(schema.hash, System.Globalization.NumberStyles.HexNumber, null, out var h) ? h : 0;
            schema.byName = new Dictionary<string, int>();
            for (var i = 0; i < schema.types.Length; i++) schema.byName[schema.types[i].name] = i;
            return schema;
        }

        // A type's index by its Tide name (Combat.Health), or -1.
        public int TypeIndex(string name)
        {
            return name != null && byName != null && byName.TryGetValue(name, out var i) ? i : -1;
        }

        public TideSchemaType Type(int index)
        {
            return (uint)index < (uint)types.Length ? types[index] : null;
        }

        public int PrefabIndex(string name)
        {
            return Array.IndexOf(prefabs, name);
        }
    }

    [Serializable]
    public sealed class TideSchemaType
    {
        public string name;
        public string kind;  // "component", "singleton", "struct", "input", "command", "event" or "local"
        public uint size;
        public int local = -1; // A match component's or singleton's local fields: their type's index, or -1
        public bool scene;     // A component that's a scene's: the tag of the entities a loaded scene holds
        public string defaults; // A component's value with its declared defaults, its bytes as hex (text and lists empty)
        public TideSchemaField[] fields = Array.Empty<TideSchemaField>();

        // `defaults` as bytes: null without them.
        public byte[] DefaultBytes()
        {
            if (string.IsNullOrEmpty(defaults) || defaults.Length != size * 2) return null;
            var bytes = new byte[size];
            for (var i = 0; i < bytes.Length; i++)
            {
                if (!byte.TryParse(defaults.Substring(i * 2, 2), System.Globalization.NumberStyles.HexNumber, null, out bytes[i])) return null;
            }
            return bytes;
        }

        public TideSchemaField Field(string fieldName)
        {
            foreach (var f in fields)
            {
                if (f.name == fieldName) return f;
            }
            return null;
        }
    }

    [Serializable]
    public sealed class TideSchemaField
    {
        public string name;
        public string type;   // Its Tide type: float3, quaternion, Body, List<int>...
        public string kind;   // "int", "float", "bool", "struct", "enum", "text", "list", "entity" or "plain"
        public uint dim;      // Numbers: how many
        public int @ref = -1; // A struct's or enum's index; a list's element type's
        public uint offset;
        public uint size;
        public uint element;  // A list's element size
    }

    [Serializable]
    public sealed class TideSchemaEnum
    {
        public string name;
        public string[] members = Array.Empty<string>();
        public int[] values = Array.Empty<int>();
    }

    [Serializable]
    public sealed class TideSchemaCommand
    {
        public string name;
        public int type; // Its value's type index
    }
}
