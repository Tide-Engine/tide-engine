using System;

namespace Tide
{
    // Names the Tide type a C# struct mirrors (generated bindings carry it):
    // its fields laid out as the game's C lays the type out.
    [AttributeUsage(AttributeTargets.Struct)]
    public sealed class TideTypeAttribute : Attribute
    {
        public readonly string Name;
        public readonly bool Local; // The type of a match component's local fields

        public TideTypeAttribute(string name, bool local = false)
        {
            Name = name;
            Local = local;
        }
    }
}
