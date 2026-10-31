using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using NUnit.Framework;
using Tide;
using Tide.Editor;

// The tide a project's Tide package builds with (TideVersions): a release tag the package follows is that version,
// another commit the version the site says, kept in Library/Tide; a version installed side by side from its release,
// checked against SHA256SUMS. The site is a folder here (TIDE_RELEASES_SITE, file://), the versions another
// (TIDE_VERSIONS).
public class VersionTests
{
    const string Version = "9.9.9-test.1";
    string scratch, site, versions;
    string savedSite, savedVersions;

    [SetUp]
    public void Make()
    {
        scratch = Path.Combine(Path.GetTempPath(), "tide-version-tests-" + Guid.NewGuid().ToString("N"));
        site = Path.Combine(scratch, "site");
        versions = Path.Combine(scratch, "versions");
        Directory.CreateDirectory(Path.Combine(site, "releases", "for"));
        savedSite = Environment.GetEnvironmentVariable("TIDE_RELEASES_SITE");
        savedVersions = Environment.GetEnvironmentVariable("TIDE_VERSIONS");
        Environment.SetEnvironmentVariable("TIDE_RELEASES_SITE", new Uri(site).AbsoluteUri);
        Environment.SetEnvironmentVariable("TIDE_VERSIONS", versions);
    }

    [TearDown]
    public void Clean()
    {
        Environment.SetEnvironmentVariable("TIDE_RELEASES_SITE", savedSite);
        Environment.SetEnvironmentVariable("TIDE_VERSIONS", savedVersions);
        var kept = Path.Combine(TideLibrary.BuildFolder, "version.json");
        if (File.Exists(kept)) File.Delete(kept);
        try
        {
            Directory.Delete(scratch, true);
        }
        catch (IOException)
        {
            // A scanner has a file a moment
        }
    }

    // A release on the site: this platform's package of `inside` (VERSION's text), and SHA256SUMS saying `sum`
    // (null: the package's own)
    void Publish(string inside, string sum = null)
    {
        var files = Path.Combine(scratch, "package");
        Directory.CreateDirectory(Path.Combine(files, "bin"));
        File.WriteAllText(Path.Combine(files, "VERSION"), inside + "\n");
        File.WriteAllText(Path.Combine(files, "bin", TideVersions.ExecutableName), "not really tide");
        var package = Path.Combine(site, TideVersions.PackageName);
        if (File.Exists(package)) File.Delete(package);
        Assume.That(package.EndsWith(".zip"), "a tar.gz is unpacked by tar: this test makes Windows' zip");
        System.IO.Compression.ZipFile.CreateFromDirectory(files, package);
        Directory.Delete(files, true);
        string hex;
        using (var sha = SHA256.Create())
        {
            var s = new StringBuilder();
            foreach (var b in sha.ComputeHash(File.ReadAllBytes(package))) s.Append(b.ToString("x2"));
            hex = s.ToString();
        }
        File.WriteAllText(Path.Combine(site, "SHA256SUMS"), $"{sum ?? hex}  {TideVersions.PackageName}\n0000  other.zip\n");
        File.WriteAllText(Path.Combine(site, "releases", $"v{Version}.json"), Release());
    }

    string Release(bool exact = true) =>
        $"{{\"tag_name\": \"v{Version}\", \"exact\": {exact.ToString().ToLowerInvariant()}, \"prerelease\": true, \"assets\": [" +
        $"{{\"name\": \"{TideVersions.PackageName}\", \"browser_download_url\": \"{new Uri(Path.Combine(site, TideVersions.PackageName)).AbsoluteUri}\"}}, " +
        $"{{\"name\": \"SHA256SUMS\", \"browser_download_url\": \"{new Uri(Path.Combine(site, "SHA256SUMS")).AbsoluteUri}\"}}]}}";

    [Test]
    public void ADevelopmentLaunchPublishesOnlyThisSessionsCommand()
    {
        var cache = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
        var saved = File.Exists(cache) ? File.ReadAllText(cache) : null;
        try
        {
            var command = Path.Combine(scratch, "dev build", TideVersions.ExecutableName);
            Directory.CreateDirectory(Path.GetDirectoryName(command));
            File.WriteAllText(command, "discovery only, never run");
            var arguments = new[] { "Unity", "-projectPath", "a project", "-tide-command", command };
            Assert.AreEqual(command, TideToolchain.Find(arguments, out var where));
            StringAssert.Contains("this editor session only", where);
            StringAssert.Contains(command.Replace("\\", "\\\\"), File.ReadAllText(cache));
            StringAssert.Contains("lockHash", File.ReadAllText(cache));
            StringAssert.Contains("\"format\":2", File.ReadAllText(cache));
            using (var process = System.Diagnostics.Process.GetCurrentProcess())
                StringAssert.Contains("\"sessionPid\":" + process.Id, File.ReadAllText(cache));
            StringAssert.Contains("\"sessionStarted\":" + TideToolchain.SessionStarted, File.ReadAllText(cache));
            StringAssert.Contains(command, TideToolchain.MismatchHint(arguments));
            StringAssert.Contains("Restart Unity without -tide-command", TideToolchain.MismatchHint(arguments));
            TideToolchain.EndSession(arguments);
            Assert.IsFalse(File.Exists(cache), "quitting removes the development selection");
            arguments[4] = command + ".missing";
            Assert.IsNull(TideToolchain.Find(arguments, out _));
            Assert.IsFalse(File.Exists(cache), "a failed selection must not leave an IDE using its old command");
        }
        finally
        {
            if (saved != null) File.WriteAllText(cache, saved);
            else if (File.Exists(cache)) File.Delete(cache);
        }
    }

    [Test]
    public void NormalLaunchIgnoresTheOldSavedOverride()
    {
        var user = Path.Combine(TideSettings.ProjectFolder, "UserSettings", "TideSettings.asset");
        var saved = File.Exists(user) ? File.ReadAllText(user) : null;
        var command = Path.Combine(scratch, "old-tide.exe");
        File.WriteAllText(command, "discovery only");
        Directory.CreateDirectory(Path.GetDirectoryName(user));
        try
        {
            File.WriteAllText(user, "tidePath: " + command + "\nmoved: 1\n");
            var normal = TideToolchain.Find(Array.Empty<string>(), out _);
            Assert.AreNotEqual(command, normal);
            if (normal != null)
            {
                var cache = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
                StringAssert.Contains("\"sessionPid\":0", File.ReadAllText(cache));
                TideToolchain.EndSession(new[] { "-tide-command", command });
                Assert.IsTrue(File.Exists(cache), "a session ending must not delete a normal selection");
            }
        }
        finally
        {
            if (saved != null) File.WriteAllText(user, saved); else File.Delete(user);
        }
    }

    [Test]
    public void TheActualLaunchArgumentSelectsTheDevelopmentCompilerOnlyWhenPresent()
    {
        var expected = Environment.GetEnvironmentVariable("TIDE_TEST_COMMAND");
        var actual = TideToolchain.LaunchCommand(Environment.GetCommandLineArgs(), out var error);
        Assert.IsNull(error);
        Assert.AreEqual(string.IsNullOrEmpty(expected) ? null : expected, actual);
        if (actual != null) Assert.AreEqual(actual, TideToolchain.Find(out _, false));
        Assert.IsNull(TideToolchain.LaunchCommand(new[] { "-tide-command" }, out error));
        Assert.IsNotNull(error);
        Assert.IsNull(TideToolchain.LaunchCommand(new[] { "-tide-command", "-batchmode" }, out error));
        Assert.IsNotNull(error);
        Assert.IsNull(TideToolchain.LaunchCommand(new[] { "-tide-command", "one", "-tide-command", "two" }, out error));
        Assert.IsNotNull(error);
    }

    [Test]
    public void AnOldHostIsRefusedBeforePlayWithTheSelectedCompilersFix()
    {
        var host = Environment.GetEnvironmentVariable("TIDE_TEST_ABI5");
        if (string.IsNullOrEmpty(host)) Assert.Ignore("Set TIDE_TEST_ABI5 to the folder of a test host exporting ABI 5.");
        var development = TideToolchain.LaunchCommand(Environment.GetCommandLineArgs(), out _) != null;
        UnityEngine.TestTools.LogAssert.Expect(UnityEngine.LogType.Error,
            new System.Text.RegularExpressions.Regex(development ? @"its ABI is 5.*-tide-command.*Restart Unity without -tide-command"
                : @"its ABI is 5.*Selected tide:.*Window > Tide > Versions"));
        Assert.IsFalse(TideBuildService.HostReady(host));
    }

    [Test]
    public void AReleaseTagIsItsVersion()
    {
        Assert.AreEqual("0.5.0-nightly.12", TideVersions.OfTag("v0.5.0-nightly.12"));
        Assert.AreEqual("0.5.0", TideVersions.OfTag("0.5.0"));
        Assert.IsNull(TideVersions.OfTag("dev"));
        Assert.IsNull(TideVersions.OfTag("4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e12345678"));
        Assert.IsNull(TideVersions.OfTag("v0.5"));
        // A tag the package follows asks the site nothing (there's nothing at it)
        Assert.AreEqual("0.5.0", TideVersions.Wanted("abc1234", "v0.5.0", out var why));
        Assert.IsNull(why);
    }

    [Test]
    public void ACommitIsTheVersionTheSiteSaysKeptInLibrary()
    {
        const string commit = "1234567890abcdef1234567890abcdef12345678";
        File.WriteAllText(Path.Combine(site, "releases", "for", commit), Release());
        Assert.AreEqual(Version, TideVersions.Wanted(commit, "dev", out var why), why);
        File.Delete(Path.Combine(site, "releases", "for", commit));
        Assert.AreEqual(Version, TideVersions.Wanted(commit, "dev", out why), "kept: the site isn't asked again");
        const string unknown = "fedcba0987654321fedcba0987654321fedcba09";
        Assert.IsNull(TideVersions.Wanted(unknown, "dev", out why));
        StringAssert.Contains("fedcba09", why);
    }

    [Test]
    public void AnAncestorReleaseIsNeverOfferedAsAMatchingCompiler()
    {
        const string commit = "234567890abcdef1234567890abcdef123456789";
        var answer = Path.Combine(site, "releases", "for", commit);
        var cache = Path.Combine(TideLibrary.BuildFolder, "version.json");
        Directory.CreateDirectory(TideLibrary.BuildFolder);
        File.WriteAllText(cache, $"{{\"commit\":\"{commit}\",\"version\":\"{Version}\"}}");
        File.WriteAllText(answer, Release(false));
        Assert.IsNull(TideVersions.Wanted(commit, "dev", out var why), "the legacy ancestor cache must be ignored");
        StringAssert.Contains("no matching compiler", why);
        StringAssert.Contains("Window > Tide > Versions", why);
        StringAssert.Contains(Version, why);
        File.WriteAllText(answer, Release());
        TideVersions.Refresh();
        Assert.AreEqual(Version, TideVersions.Wanted(commit, "dev", out why), why);
    }

    [Test]
    public void AnOldServerAnswerDoesNotProveAMatchAndExpiredAnswersAreRefreshed()
    {
        const string commit = "34567890abcdef1234567890abcdef1234567890";
        var answer = Path.Combine(site, "releases", "for", commit);
        File.WriteAllText(answer, Release().Replace("\"exact\": true, ", ""));
        Assert.IsNull(TideVersions.Wanted(commit, "dev", out _));
        TideVersions.Refresh();
        Directory.CreateDirectory(TideLibrary.BuildFolder);
        File.WriteAllText(Path.Combine(TideLibrary.BuildFolder, "version.json"),
            $"{{\"format\":2,\"commit\":\"{commit}\",\"version\":\"1.0.0\",\"checkedAt\":0}}");
        File.WriteAllText(answer, Release());
        Assert.AreEqual(Version, TideVersions.Wanted(commit, "dev", out var why), why);
    }

    [Test]
    public void VersionChoicesKeepTheRepositoryAndPackagePath()
    {
        const string package = "com.tide.unity@https://github.com/Tide-Engine/tide-engine.git?path=/unity/com.tide.unity";
        Assert.AreEqual(package.Substring("com.tide.unity@".Length) + "#v" + Version,
            TideVersionsWindow.PackageUrl(package + "#dev", "v" + Version));
        Assert.AreEqual(package.Substring("com.tide.unity@".Length) + "#dev",
            TideVersionsWindow.PackageUrl(package + "#v1.0.0", "dev"));
        Assert.Throws<ArgumentException>(() => TideVersionsWindow.PackageUrl("com.tide.unity@file:../local", "dev"));
        Assert.Throws<ArgumentException>(() => TideVersionsWindow.PackageUrl(package, "not-a-version"));
        Assert.Greater(TideVersionsWindow.CompareVersions("v1.0.0-nightly.10", "v1.0.0-nightly.9"), 0);
        Assert.Greater(TideVersionsWindow.CompareVersions("v1.0.0", "v1.0.0-nightly.10"), 0);
        Assert.Greater(TideVersionsWindow.CompareVersions("v1.1.0-nightly.1", "v1.0.0"), 0);
        File.WriteAllText(Path.Combine(site, "releases.json"), "[" + Release() + "]");
        var releases = TideVersions.Published();
        Assert.AreEqual(1, releases.Length);
        Assert.IsTrue(TideVersions.CanInstall(releases[0]));
        releases[0].assets = Array.Empty<TideVersions.Asset>();
        Assert.IsFalse(TideVersions.CanInstall(releases[0]));
        Assert.IsTrue(UnityEditor.Menu.GetEnabled("Window/Tide/Versions"));
    }

    [Test]
    public void AVersionInstallsBesideTheOthers()
    {
        Publish(Version);
        Assert.IsNull(TideVersions.Executable(Version));
        var made = TideVersions.Install(Version, null, out var error);
        Assert.IsNull(error);
        Assert.AreEqual(Path.Combine(versions, Version, "bin", TideVersions.ExecutableName), made);
        Assert.AreEqual(made, TideVersions.Executable(Version));
        Assert.AreEqual(made, TideVersions.Install(Version, null, out _), "installed once");
        Assert.AreEqual(1, Directory.GetDirectories(versions).Length, "no unpacking's folder left");
        // Another beside it doesn't touch it
        Assert.IsTrue(File.Exists(made));
    }

    [Test]
    public void ADamagedOrWrongDownloadInstallsNothing()
    {
        Publish(Version, new string('0', 64));
        Assert.IsNull(TideVersions.Install(Version, null, out var error));
        StringAssert.Contains("damaged", error);
        Publish("1.0.0");
        Assert.IsNull(TideVersions.Install(Version, null, out error));
        StringAssert.Contains("another version", error);
        Assert.IsNull(TideVersions.Executable(Version));
        Assert.IsFalse(Directory.Exists(versions) && Directory.GetDirectories(versions).Length > 0, "nothing left in versions");
    }

    [Test]
    public void AFailedCompilerDownloadLeavesThePackagePinUnchanged()
    {
        Publish(Version, new string('0', 64));
        var manifest = Path.Combine(TideSettings.ProjectFolder, "Packages", "manifest.json");
        var before = File.ReadAllText(manifest);
        var editing = TideSettings.instance.buildWhileEditing;
        TideSettings.instance.buildWhileEditing = false;
        var window = UnityEngine.ScriptableObject.CreateInstance<TideVersionsWindow>();
        try
        {
            window.Change("com.tide.unity@https://github.com/Tide-Engine/tide-engine.git?path=/unity/com.tide.unity#dev", "v" + Version);
            Assert.IsFalse(TideVersionsWindow.Switching);
            Assert.AreEqual(before, File.ReadAllText(manifest));
            Assert.IsNull(TideVersions.Executable(Version));
            var error = (string)typeof(TideVersionsWindow).GetField("error", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(window);
            StringAssert.Contains("damaged", error);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(window);
            TideSettings.instance.buildWhileEditing = editing;
        }
    }
}
