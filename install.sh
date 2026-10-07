#!/bin/sh
# Installs tide for this user, on Linux or macOS (Apple Silicon):
#
#     curl -fsSL https://tide-engine.dev/install.sh | sh
#
# Nightly versions instead: ... | TIDE_CHANNEL=nightly sh
# tide goes in ~/.tide, and its bin folder on PATH through the shell's profile.
# No root needed. Once installed, `tide upgrade` keeps it up to date.
set -eu

releases="https://tide-engine.dev/releases.json"
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) package="tide-linux-x64.tar.gz" ;;
    Darwin-arm64) package="tide-macos-arm64.tar.gz" ;;
    *) echo "There's no tide package for $(uname -s) on $(uname -m) yet." >&2; exit 1 ;;
esac
channel="stable"
[ "${TIDE_CHANNEL:-}" = "nightly" ] && channel="nightly"
root="$HOME/.tide"
bin="$root/bin"

installed=no
if [ -x "$bin/tide" ]; then
    installed=yes
    echo "tide is already installed in $root; upgrading it."
    # It keeps its channel, unless TIDE_CHANNEL says which.
    if [ -z "${TIDE_CHANNEL:-}" ] && [ -f "$root/channel" ]; then
        saved_channel="$(cat "$root/channel")"
        case "$saved_channel" in stable|nightly) channel="$saved_channel" ;; esac
    fi
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A download is a redirect to where the file is kept, which has to be https.
download() {
    curl -q -fsSL --proto-redir =https "$1" -o "$2"
}

# The channel's highest version with this platform's package, not the last one
# published (as tide upgrade picks, compiler/cli/release.c): some nightlies
# have none for macOS. Nightly takes stable releases too, when they're newer.
download "$releases" "$work/releases.json"
tag=$(python3 - "$work/releases.json" "$channel" "$package" "$work" <<'EOF'
import json, re, sys
releases = json.load(open(sys.argv[1]))

# Semantic versioning's order: a pre-release comes before its release, and
# its parts compare as numbers when they are, before words.
def order(tag):
    m = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+.*)?", tag or "")
    if not m:
        return None
    pre = m.group(4)
    parts = [(0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split(".")] if pre else []
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), pre is None, parts)

best = None
for r in releases:
    key = order(r.get("tag_name"))
    if key is None or r.get("draft") or (sys.argv[2] == "stable" and r.get("prerelease")):
        continue
    if sys.argv[3] not in [a.get("name") for a in r.get("assets") or []]:
        continue
    if best is None or key > best[0]:
        best = (key, r)
if best:
    release = best[1]
    assets = {a.get("name"): a for a in release.get("assets") or []}
    for name, file in [(sys.argv[3], "package.url"), ("SHA256SUMS", "sums.url")]:
        with open(sys.argv[4] + "/" + file, "w") as out:
            out.write(assets.get(name, {}).get("browser_download_url") or "")
    with open(sys.argv[4] + "/selected.json", "w") as out:
        json.dump(release, out)
    print(release["tag_name"])
EOF
)
if [ -z "$tag" ]; then
    echo "There's no $channel release of tide for this platform yet." >&2
    exit 1
fi

echo "Downloading tide ${tag#v}..."
package_url="$(cat "$work/package.url")"
sums_url="$(cat "$work/sums.url")"
if [ -z "$package_url" ] || [ -z "$sums_url" ]; then
    echo "Release $tag has no $package or checksums." >&2
    exit 1
fi
download "$package_url" "$work/$package"
download "$sums_url" "$work/SHA256SUMS"

# The download must match the checksum published with it.
expected="$(grep " $package\$" "$work/SHA256SUMS" | cut -d' ' -f1)"
if command -v sha256sum >/dev/null; then
    actual="$(sha256sum "$work/$package" | cut -d' ' -f1)"
else
    actual="$(shasum -a 256 "$work/$package" | cut -d' ' -f1)" # macOS
fi
if [ -z "$expected" ] || [ "$expected" != "$actual" ]; then
    echo "The download is damaged (its checksum doesn't match). Nothing was installed." >&2
    exit 1
fi

if [ "$installed" = yes ]; then
    # The installed tide's own updater takes it from here, with the files
    # checked above: it replaces running programs and updates the editors,
    # whichever version it is (older ones look for releases elsewhere).
    releases_url=$(python3 - "$work" "$package" <<'EOF'
import json, pathlib, sys
work = pathlib.Path(sys.argv[1]).resolve()
release = json.loads((work / "selected.json").read_text())
release["assets"] = [{"name": name, "size": (work / name).stat().st_size,
                      "browser_download_url": (work / name).as_uri()}
                     for name in [sys.argv[2], "SHA256SUMS"]]
(work / "local.json").write_text(json.dumps([release]))
print((work / "local.json").as_uri())
EOF
)
    if [ -n "${TIDE_CHANNEL:-}" ]; then
        TIDE_RELEASES_URL="$releases_url" "$bin/tide" upgrade "--$channel"
    else
        TIDE_RELEASES_URL="$releases_url" "$bin/tide" upgrade
    fi
    "$bin/tide" editors || true
    exit 0
fi

mkdir -p "$root"
tar -xzf "$work/$package" -C "$root"
printf '%s' "$channel" > "$root/channel"

# tide's bin folder on PATH, for new terminals: in each shell profile there is,
# and in the login shell's own, made when it has none (a new Mac's zsh has no
# ~/.zshrc). macOS's bash reads ~/.bash_profile or ~/.profile, and one made
# would hide the other, so it gets ~/.profile only when it has neither.
line="export PATH=\"\$HOME/.tide/bin:\$PATH\""
case "$(basename "${SHELL:-sh}")" in
    zsh) touch "$HOME/.zshrc" ;;
    bash)
        if [ "$(uname -s)" != "Darwin" ]; then
            touch "$HOME/.bashrc"
        elif [ ! -f "$HOME/.bash_profile" ] && [ ! -f "$HOME/.bash_login" ] && [ ! -f "$HOME/.profile" ]; then
            touch "$HOME/.profile"
        fi
        ;;
esac
if [ "$(basename "${SHELL:-sh}")" = "fish" ] || [ -d "$HOME/.config/fish" ]; then
    mkdir -p "$HOME/.config/fish/conf.d"
    printf '# tide\ncontains -- "$HOME/.tide/bin" $PATH; or set -gx PATH "$HOME/.tide/bin" $PATH\n' \
        > "$HOME/.config/fish/conf.d/tide.fish"
fi
for profile in "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.bash_login" "$HOME/.zshrc" "$HOME/.profile"; do
    if [ -f "$profile" ] && ! grep -qs '.tide/bin' "$profile"; then
        printf '\n# tide\n%s\n' "$line" >> "$profile"
    fi
done

echo "Installed tide in $root."
# Tide in VS Code and the editors like it.
"$bin/tide" editors || true
if [ "$(uname -s)" = "Darwin" ]; then
    echo "Native games need Apple's command-line tools: xcode-select --install"
else
    echo "Native games need your distribution's C development files, which come with gcc (build-essential on Debian and Ubuntu)."
fi
echo "Open a new terminal, go to a folder with .tide files and run: tide run"
