#!/bin/sh
# Installs tide for this user, on Linux or macOS (Apple Silicon):
#
#     gh api -H 'Accept: application/vnd.github.raw+json' 'repos/BlenMiner/tide-engine/contents/install.sh?ref=dev' | sh
#
# Nightly versions instead: ... | TIDE_CHANNEL=nightly sh
# tide goes in ~/.tide, and its bin folder on PATH through the shell's profile.
# No root needed. Once installed, `tide upgrade` keeps it up to date.
# Private releases use GH_TOKEN, then GITHUB_TOKEN, then `gh auth login`.
set -eu

repo="BlenMiner/tide-engine"
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) package="tide-linux-x64.tar.gz" ;;
    Darwin-arm64) package="tide-macos-arm64.tar.gz" ;;
    *) echo "There's no tide package for $(uname -s) on $(uname -m) yet." >&2; exit 1 ;;
esac
channel="stable"
[ "${TIDE_CHANNEL:-}" = "nightly" ] && channel="nightly"
root="$HOME/.tide"
bin="$root/bin"

# Credentials already used by GitHub's CLI also work for private releases.
token="${GH_TOKEN:-${GITHUB_TOKEN:-}}"
if [ -z "$token" ] && command -v gh >/dev/null 2>&1; then
    token="$(gh auth token --hostname github.com 2>/dev/null || true)"
fi
if [ "$(printf '%s' "$token" | LC_ALL=C tr -d '!-~' | wc -c)" -ne 0 ]; then
    echo 'The GitHub token must contain only printable non-space ASCII characters.' >&2
    exit 1
fi
installed=no
if [ -x "$bin/tide" ]; then
    installed=yes
    echo "tide is already installed in $root; upgrading it."
    # It keeps its channel, unless TIDE_CHANNEL says which.
    if [ -z "$token" ]; then
        if [ -n "${TIDE_CHANNEL:-}" ]; then
            "$bin/tide" upgrade "--$channel"
        else
            "$bin/tide" upgrade
        fi
        # Tide in the editors installed since.
        "$bin/tide" editors || true
        exit 0
    fi
    if [ -z "${TIDE_CHANNEL:-}" ] && [ -f "$root/channel" ]; then
        saved_channel="$(cat "$root/channel")"
        case "$saved_channel" in stable|nightly) channel="$saved_channel" ;; esac
    fi
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# curl reads credentials from a private file, never its command line. It
# drops Authorization when an asset redirects to another host.
authenticated=no
if [ -n "$token" ]; then
    authenticated=yes
    (umask 077; printf 'Authorization: Bearer %s\n' "$token" > "$work/auth")
fi
download() {
    case "$authenticated:$1" in
        yes:https://api.github.com/*)
            curl -q -fsSL --proto-redir =https -H "@$work/auth" -H "Accept: $3" "$1" -o "$2" ;;
        *) curl -q -fsSL "$1" -o "$2" ;;
    esac
}

# The channel's highest version with this platform's package, not the last one
# published (as tide upgrade picks, compiler/cli/release.c): some old
# nightlies have none for macOS. Nightly takes stable releases
# too, when they're newer. Stable also asks for GitHub's latest release, since
# nightly ones can push it out of the list.
download "https://api.github.com/repos/$repo/releases?per_page=100" "$work/releases.json" application/vnd.github+json
echo '{}' > "$work/latest.json"
if [ "$channel" = "stable" ]; then
    download "https://api.github.com/repos/$repo/releases/latest" "$work/latest.json" application/vnd.github+json ||
        echo '{}' > "$work/latest.json"
fi
tag=$(python3 - "$work/releases.json" "$work/latest.json" "$channel" "$package" "$authenticated" "$work" <<'EOF'
import json, re, sys
releases = json.load(open(sys.argv[1])) + [json.load(open(sys.argv[2]))]

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
    if key is None or r.get("draft") or (sys.argv[3] == "stable" and r.get("prerelease")):
        continue
    if sys.argv[4] not in [a.get("name") for a in r.get("assets") or []]:
        continue
    if best is None or key > best[0]:
        best = (key, r)
if best:
    release = best[1]
    field = "url" if sys.argv[5] == "yes" else "browser_download_url"
    assets = {a.get("name"): a for a in release.get("assets") or []}
    for name, file in [(sys.argv[4], "package.url"), ("SHA256SUMS", "sums.url")]:
        with open(sys.argv[6] + "/" + file, "w") as out:
            out.write(assets.get(name, {}).get(field) or "")
    with open(sys.argv[6] + "/selected.json", "w") as out:
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
download "$package_url" "$work/$package" application/octet-stream
download "$sums_url" "$work/SHA256SUMS" application/octet-stream

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
    # The updater of an older tide needs no authentication for checked local
    # files, and still replaces running programs and updates the editors.
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
