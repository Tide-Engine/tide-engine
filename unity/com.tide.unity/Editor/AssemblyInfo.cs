using System.Runtime.CompilerServices;

// The Sample's editor tests drive the inspector's Reset and Revert to Prefab as its menus do, and log as the build service does
[assembly: InternalsVisibleTo("Tide.Sample.EditorTests")]
// The Android build's part, an assembly compiled only where Unity's Android module is
[assembly: InternalsVisibleTo("Tide.Editor.Android")]
