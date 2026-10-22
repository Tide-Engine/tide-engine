using System.Collections.Generic;
using NUnit.Framework;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

// The shaders tide unity wrote into com.tide.game (Resources/TideShaders) and the package's own compile for this
// editor's graphics API with nothing said: a warning would be in the user's Console.
public class ShaderTests
{
    [Test]
    public void TideShadersCompileWithNothingSaid()
    {
        if (SystemInfo.graphicsDeviceType == GraphicsDeviceType.Null) Assert.Ignore("no GPU in this run (-nographics)");
        var shaders = new List<Shader>(Resources.LoadAll<Shader>("TideShaders")) { Resources.Load<Shader>("TideResolveDepth") };
        Assert.Greater(shaders.Count, 10, "the renderer's shaders and the game's are there");
        var said = new List<string>();
        foreach (var shader in shaders)
        {
            Assert.IsNotNull(shader);
            var material = new Material(shader);
            ShaderUtil.CompilePass(material, 0, true);
            foreach (var message in ShaderUtil.GetShaderMessages(shader)) said.Add($"{shader.name}: {message.severity} {message.message} ({message.platform})");
            Object.DestroyImmediate(material);
        }
        Assert.IsEmpty(said, string.Join("\n", said));
    }
}
