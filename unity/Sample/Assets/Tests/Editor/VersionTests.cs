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

    string Release() =>
        $"{{\"tag_name\": \"v{Version}\", \"prerelease\": true, \"assets\": [" +
        $"{{\"name\": \"{TideVersions.PackageName}\", \"browser_download_url\": \"{new Uri(Path.Combine(site, TideVersions.PackageName)).AbsoluteUri}\"}}, " +
        $"{{\"name\": \"SHA256SUMS\", \"browser_download_url\": \"{new Uri(Path.Combine(site, "SHA256SUMS")).AbsoluteUri}\"}}]}}";

    [Test]
    public void AnOverrideExplainsTheFixAndPublishesTheSameCommandForTheIde()
    {
        var user = TideUserSettings.Loaded;
        var previous = user.tidePath;
        var cache = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
        var saved = File.Exists(cache) ? File.ReadAllText(cache) : null;
        try
        {
            var command = Path.Combine(scratch, "bin", TideVersions.ExecutableName);
            Directory.CreateDirectory(Path.GetDirectoryName(command));
            File.WriteAllText(command, "discovery only, never run");
            user.tidePath = command;
            Assert.AreEqual(command, TideToolchain.Find(out _, false));
            StringAssert.Contains(command.Replace("\\", "\\\\"), File.ReadAllText(cache));
            StringAssert.Contains("lockHash", File.ReadAllText(cache));
            StringAssert.Contains(command, TideToolchain.MismatchHint());
            StringAssert.Contains("Use package version", TideToolchain.MismatchHint());
            user.tidePath = command + ".missing";
            Assert.IsNull(TideToolchain.Find(out _, false));
            Assert.IsFalse(File.Exists(cache), "a failed selection must not leave an IDE using its old command");
        }
        finally
        {
            user.tidePath = previous;
            if (saved != null) File.WriteAllText(cache, saved);
            else if (File.Exists(cache)) File.Delete(cache);
        }
    }

    [Test]
    public void AnOldHostIsRefusedBeforePlayWithTheOverrideAndItsFix()
    {
        var host = Environment.GetEnvironmentVariable("TIDE_TEST_ABI5");
        if (string.IsNullOrEmpty(host)) Assert.Ignore("Set TIDE_TEST_ABI5 to the folder of a test host exporting ABI 5.");
        var user = TideUserSettings.Loaded;
        var previous = user.tidePath;
        try
        {
            user.tidePath = Path.Combine(scratch, "old-tide.exe");
            UnityEngine.TestTools.LogAssert.Expect(UnityEngine.LogType.Error,
                new System.Text.RegularExpressions.Regex(@"its ABI is 5.*Your tide command overrides.*old-tide.exe.*Use package version"));
            Assert.IsFalse(TideBuildService.HostReady(host));
        }
        finally { user.tidePath = previous; }
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
}
