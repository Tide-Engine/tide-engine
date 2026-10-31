using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.PackageManager;
using UnityEngine;
using UnityEngine.Networking;
using Debug = UnityEngine.Debug;

namespace Tide.Editor
{
    // The tide this package builds with: the one its own commit is. Unity locks a git package's commit in the
    // project's Packages/packages-lock.json, so a branch of the project has one: a release tag the package follows
    // (#v0.5.0-nightly.12) is that version, and any other commit the version the site says it is, the highest
    // release at exactly it (tide-engine.dev/releases/for/<commit>). An ancestor release is only a suggestion for
    // switching both the package and compiler in Tide Versions. Versions are installed side by side, each
    // whole, in TIDE_VERSIONS or the versions folder of where tide's installer puts tide
    // (%LOCALAPPDATA%\Tide\versions\<version>, ~/.tide/versions/<version>), downloaded from the site once, and
    // checked against the release's SHA256SUMS. A package in a folder (file:, embedded) has no commit: the tide set
    // by the session's -tide-command argument, on PATH or installed is its.
    public static class TideVersions
    {
        static readonly Regex Tag = new Regex(@"^v?(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$");

        static string Site => (Environment.GetEnvironmentVariable("TIDE_RELEASES_SITE") ?? "https://tide-engine.dev").TrimEnd('/');

        public static string Root
        {
            get
            {
                var set = Environment.GetEnvironmentVariable("TIDE_VERSIONS");
                if (!string.IsNullOrEmpty(set)) return Path.GetFullPath(set);
                return Path.Combine(Installed, "versions");
            }
        }

        // Where tide's installer puts tide
        public static string Installed => Application.platform == RuntimePlatform.WindowsEditor
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tide")
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".tide");

        public static string ExecutableName => Application.platform == RuntimePlatform.WindowsEditor ? "tide.exe" : "tide";

        // The version a revision of the package is, when it's a release tag: v0.5.0 is 0.5.0; null for a branch or a
        // commit
        public static string OfTag(string revision)
        {
            var m = Tag.Match(revision ?? "");
            return m.Success ? m.Groups[1].Value : null;
        }

        // This platform's package of tide, as a release has it
        public static string PackageName => Application.platform switch
        {
            RuntimePlatform.WindowsEditor => "tide-windows-x64.zip",
            RuntimePlatform.OSXEditor => "tide-macos-arm64.tar.gz",
            _ => "tide-linux-x64.tar.gz",
        };

        // The package's git commit and what it follows, or false for a package that isn't from git
        public static bool PackageCommit(out string commit, out string revision)
        {
            commit = revision = null;
            var info = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(TideVersions).Assembly);
            if (info == null || info.source != PackageSource.Git || info.git == null) return false;
            commit = info.git.hash;
            revision = info.git.revision;
            return !string.IsNullOrEmpty(commit);
        }

        // The version this package builds with, or null for one that isn't from git (`why` says, or why it isn't
        // known: the site can't be reached and nothing's kept)
        public static string Wanted(out string why)
        {
            why = null;
            if (!PackageCommit(out var commit, out var revision)) return null;
            return Wanted(commit, revision, out why);
        }

        static string CachePath => Path.Combine(TideLibrary.BuildFolder, "version.json");

        [Serializable]
        sealed class Cached
        {
            public int format;
            public string commit, version;
            public long checkedAt;
        }

        // A commit the site couldn't be asked about lately, and why: asked again a minute on, not at each repaint
        static string failedCommit, failedWhy;
        static DateTime failedAt;

        public static string Wanted(string commit, string revision, out string why)
        {
            why = null;
            var tagged = OfTag(revision);
            if (tagged != null) return tagged;
            if (commit == failedCommit && DateTime.UtcNow - failedAt < TimeSpan.FromMinutes(1))
            {
                why = failedWhy;
                return null;
            }
            try
            {
                var kept = JsonUtility.FromJson<Cached>(File.ReadAllText(CachePath));
                if (kept != null && kept.format == 2 && kept.commit == commit && !string.IsNullOrEmpty(kept.version)
                    && DateTimeOffset.UtcNow.ToUnixTimeSeconds() - kept.checkedAt < 600) return kept.version;
            }
            catch (Exception)
            {
                // None kept yet
            }
            try
            {
                var answer = Encoding.UTF8.GetString(Fetch($"{Site}/releases/for/{commit}"));
                var release = JsonUtility.FromJson<Release>(answer);
                var tag = release?.tag_name;
                var version = OfTag(tag);
                if (version == null)
                {
                    why = $"the site said no version of tide for the package's commit {Short(commit)}";
                    Failed(commit, why);
                    return null;
                }
                if (!release.exact)
                {
                    why = $"no matching compiler is published for package commit {Short(commit)}. " +
                        $"Open Window > Tide > Versions to switch the package and compiler together to {version} or another release, " +
                        "or check again after publication. For engine development, launch Unity with -tide-command <path>";
                    Failed(commit, why);
                    return null;
                }
                Directory.CreateDirectory(TideLibrary.BuildFolder);
                File.WriteAllText(CachePath, JsonUtility.ToJson(new Cached { format = 2, commit = commit, version = version,
                    checkedAt = DateTimeOffset.UtcNow.ToUnixTimeSeconds() }));
                return version;
            }
            catch (Exception e)
            {
                why = $"which tide the package's commit {Short(commit)} is isn't known: {Message(e)}";
                Failed(commit, why);
                return null;
            }
        }

        static void Failed(string commit, string why)
        {
            failedCommit = commit;
            failedWhy = why;
            failedAt = DateTime.UtcNow;
        }

        public static void Refresh()
        {
            failedCommit = failedWhy = null;
            if (File.Exists(CachePath)) File.Delete(CachePath);
        }

        static string Short(string commit) => commit != null && commit.Length > 8 ? commit.Substring(0, 8) : commit;

        static string Message(Exception e) => e is AggregateException a && a.InnerException != null ? a.InnerException.Message : e.Message;

        // The tide command of `version` on this machine, or null: installed side by side, or the one tide's installer
        // put in place being that version
        public static string Executable(string version)
        {
            var side = Path.Combine(Root, version, "bin", ExecutableName);
            if (File.Exists(side) && IsWhole(Path.Combine(Root, version), version)) return side;
            var main = Path.Combine(Installed, "bin", ExecutableName);
            if (File.Exists(main) && IsWhole(Installed, version)) return main;
            return null;
        }

        static bool IsWhole(string folder, string version)
        {
            try
            {
                return File.ReadAllText(Path.Combine(folder, "VERSION")).Trim() == version;
            }
            catch (Exception)
            {
                return false;
            }
        }

        [Serializable]
        internal sealed class Asset
        {
            public string name, browser_download_url;
        }

        [Serializable]
        internal sealed class Release
        {
            public string tag_name;
            public bool exact, prerelease;
            public Asset[] assets;
        }

        [Serializable]
        sealed class ReleaseList { public Release[] releases; }

        internal static Release[] Published()
            => JsonUtility.FromJson<ReleaseList>("{\"releases\":" + Encoding.UTF8.GetString(Fetch($"{Site}/releases.json")) + "}")?.releases
                ?? Array.Empty<Release>();

        internal static bool CanInstall(Release release)
            => OfTag(release.tag_name) != null && Array.Exists(release.assets ?? Array.Empty<Asset>(), a => a.name == PackageName)
                && Array.Exists(release.assets ?? Array.Empty<Asset>(), a => a.name == "SHA256SUMS");

        // An address's bytes, waited for on the editor's thread: the site's, or a file's (file://, for tests and
        // mirrors). Through UnityWebRequest, as .NET's HttpClient in the editor can time out on every address.
        static byte[] Fetch(string address)
        {
            var uri = new Uri(address);
            if (uri.IsFile) return File.ReadAllBytes(uri.LocalPath);
            using var request = UnityWebRequest.Get(uri);
            request.timeout = 20;
            request.SendWebRequest();
            while (!request.isDone) Thread.Sleep(10);
            if (request.result != UnityWebRequest.Result.Success) throw new IOException($"{address}: {request.error}");
            return request.downloadHandler.data;
        }

        // A version's package for this platform, downloading into a folder of its own beside the versions
        sealed class Download
        {
            public string version, work, archive, expected;
            public UnityWebRequest request; // Null for a file's (file://), copied at once

            public bool Done => request == null || request.isDone;
            public float Got => request?.downloadProgress ?? 1f;
        }

        // Asks the site for `version`'s release and starts downloading its package, on the editor's thread: null,
        // with `error` said, for a release with none
        static Download Start(string version, out string error)
        {
            error = null;
            var release = JsonUtility.FromJson<Release>(Encoding.UTF8.GetString(Fetch($"{Site}/releases/v{version}.json")));
            Asset package = null, sums = null;
            foreach (var a in release?.assets ?? Array.Empty<Asset>())
            {
                if (a.name == PackageName) package = a;
                if (a.name == "SHA256SUMS") sums = a;
            }
            if (package == null || sums == null)
            {
                error = $"tide {version} has no {PackageName} to install";
                return null;
            }
            var d = new Download
            {
                version = version,
                work = Path.Combine(Root, $".{version}-{Guid.NewGuid():N}"),
                expected = Expected(Encoding.UTF8.GetString(Fetch(sums.browser_download_url)), PackageName),
            };
            Directory.CreateDirectory(d.work);
            d.archive = Path.Combine(d.work, PackageName);
            var uri = new Uri(package.browser_download_url);
            if (uri.IsFile)
            {
                File.Copy(uri.LocalPath, d.archive);
                return d;
            }
            d.request = new UnityWebRequest(uri, UnityWebRequest.kHttpVerbGET, new DownloadHandlerFile(d.archive), null);
            d.request.SendWebRequest();
            return d;
        }

        // Whether the download came whole, read on the editor's thread
        static bool Downloaded(Download d, out string error)
        {
            error = null;
            if (d.request == null || d.request.result == UnityWebRequest.Result.Success) return true;
            error = $"tide {d.version} couldn't be downloaded: {d.request.error}";
            return false;
        }

        // Checks the downloaded package against the release's SHA256SUMS and unpacks it whole into its folder, which
        // takes the version's place last, on any thread: the tide command, or null with `error` said
        static string Settle(Download d, out string error)
        {
            error = null;
            string actual;
            using (var sha = SHA256.Create())
            using (var file = File.OpenRead(d.archive))
                actual = Hex(sha.ComputeHash(file));
            if (d.expected == null || actual != d.expected)
            {
                error = $"the download of tide {d.version} is damaged (its checksum isn't the release's): nothing was installed";
                return null;
            }
            var into = Path.Combine(d.work, "tide");
            Directory.CreateDirectory(into);
            Unpack(d.archive, into);
            if (!IsWhole(into, d.version))
            {
                error = $"tide {d.version}'s package says it's another version: nothing was installed";
                return null;
            }
            var place = Path.Combine(Root, d.version);
            try
            {
                if (Directory.Exists(place)) Directory.Delete(place, true); // One that stopped partway: not whole
                Directory.Move(into, place);
            }
            catch (Exception)
            {
                // Another editor may have installed it meanwhile
                var other = Executable(d.version);
                if (other != null) return other;
                throw;
            }
            return Executable(d.version);
        }

        static void Clean(Download d)
        {
            if (d == null) return;
            d.request?.Dispose();
            try
            {
                if (Directory.Exists(d.work)) Directory.Delete(d.work, true);
            }
            catch (Exception)
            {
                // A scanner has a file a moment: the next install's folder is another
            }
        }

        // Installs `version` side by side, waiting for it on the editor's thread: its release's package for this
        // platform, checked against the release's SHA256SUMS, unpacked whole into a folder of its own, which takes the
        // version's place last. The tide command, or null with `error` said. `progress` hears how far, from 0 to 1,
        // and what's being done.
        public static string Install(string version, Action<float, string> progress, out string error)
        {
            error = null;
            var had = Executable(version);
            if (had != null) return had;
            Download d = null;
            try
            {
                progress?.Invoke(0f, $"Finding tide {version}");
                d = Start(version, out error);
                if (d == null) return null;
                while (!d.Done)
                {
                    progress?.Invoke(0.9f * d.Got, $"Downloading tide {version}");
                    Thread.Sleep(20);
                }
                if (!Downloaded(d, out error)) return null;
                progress?.Invoke(0.9f, $"Unpacking tide {version}");
                var exe = Settle(d, out error);
                if (exe != null) progress?.Invoke(1f, $"Installed tide {version}");
                return exe;
            }
            catch (Exception e)
            {
                var other = Executable(version);
                if (other != null) return other;
                error = $"tide {version} couldn't be installed: {Message(e)}";
                return null;
            }
            finally
            {
                Clean(d);
            }
        }

        // The checksum SHA256SUMS gives `name`, in lowercase hex
        static string Expected(string sums, string name)
        {
            foreach (var line in sums.Split('\n'))
            {
                var parts = line.Trim().Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length == 2 && parts[1].TrimStart('*') == name) return parts[0].ToLowerInvariant();
            }
            return null;
        }

        static string Hex(byte[] bytes)
        {
            var s = new StringBuilder(bytes.Length * 2);
            foreach (var b in bytes) s.Append(b.ToString("x2"));
            return s.ToString();
        }

        // A zip in place (Windows), a tar.gz by tar, which keeps what's run as runnable
        static void Unpack(string archive, string into)
        {
            if (archive.EndsWith(".zip", StringComparison.Ordinal))
            {
                System.IO.Compression.ZipFile.ExtractToDirectory(archive, into);
                return;
            }
            var tar = new ProcessStartInfo("tar", $"-xzf \"{archive}\" -C \"{into}\"")
            {
                UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true,
            };
            using var process = Process.Start(tar);
            var said = process.StandardError.ReadToEnd();
            process.WaitForExit();
            if (process.ExitCode != 0) throw new IOException($"tar couldn't unpack it: {said.Trim()}");
        }

        // In the editor, the package's version installs once someone says so, while the editor goes on: asked once a
        // session, with the editor's progress in its status bar. A batch run (a build server) installs it at once.
        static string installingVersion;
        static bool asked;

        // The tide command of the package's version, installing it if it isn't here: null meanwhile, or if it can't
        // be, with `where` saying why
        // `wait`: installed now, with a progress bar, as a player build waits for it anyway.
        public static string Ensure(string version, out string where, bool wait = false)
        {
            var exe = Executable(version);
            if (exe != null)
            {
                where = $"tide {version}, which this project's Tide package is";
                return exe;
            }
            if (Application.isBatchMode || wait)
            {
                try
                {
                    exe = Install(version, Application.isBatchMode ? null : (f, what) => EditorUtility.DisplayProgressBar("Tide", what, f),
                        out var error);
                    where = exe != null ? $"tide {version}, installed for this project's Tide package" : error;
                }
                finally
                {
                    if (!Application.isBatchMode) EditorUtility.ClearProgressBar();
                }
                return exe;
            }
            if (installingVersion != null)
            {
                where = $"Installing tide {installingVersion}, which this project's Tide package is";
                return null;
            }
            if (!asked)
            {
                asked = true;
                if (EditorUtility.DisplayDialog("Tide",
                        $"This project's Tide package builds with tide {version}, which isn't on this machine. Install it? " +
                        $"It goes beside any other tide, in {Root}.", "Install", "Not now"))
                {
                    Begin(version);
                    where = $"Installing tide {version}, which this project's Tide package is";
                    return null;
                }
            }
            where = $"This project's Tide package builds with tide {version}, which isn't installed: install it in Project Settings > Tide";
            return null;
        }

        // Starts installing `version` while the editor goes on: the download followed each update, checking and
        // unpacking it off the editor's thread; tide's build starts once it's in place
        public static void Begin(string version)
        {
            if (installingVersion != null) return;
            installingVersion = version;
            var task = Progress.Start("Tide", $"Installing tide {version}");
            Download d = null;
            Task<string> settling = null;
            void End(string exe, string why)
            {
                EditorApplication.update -= Watch;
                Progress.Finish(task, exe != null ? Progress.Status.Succeeded : Progress.Status.Failed);
                Progress.Remove(task);
                Clean(d);
                installingVersion = null;
                if (exe != null) TideBuildService.Restart();
                else Debug.LogError($"Tide: {why}");
            }
            void Watch()
            {
                if (settling == null)
                {
                    if (!d.Done)
                    {
                        Progress.Report(task, 0.9f * d.Got, $"Downloading tide {version}");
                        return;
                    }
                    if (!Downloaded(d, out var why))
                    {
                        End(null, why);
                        return;
                    }
                    Progress.Report(task, 0.9f, $"Unpacking tide {version}");
                    settling = Task.Run(() => Settle(d, out var error) ?? throw new IOException(error));
                    return;
                }
                if (!settling.IsCompleted) return;
                if (settling.IsFaulted) End(Executable(version), $"tide {version} couldn't be installed: {Message(settling.Exception)}");
                else End(settling.Result, null);
            }
            try
            {
                d = Start(version, out var error);
                if (d == null)
                {
                    End(null, error);
                    return;
                }
            }
            catch (Exception e)
            {
                End(null, $"tide {version} couldn't be installed: {Message(e)}");
                return;
            }
            EditorApplication.update += Watch;
        }
    }
}
