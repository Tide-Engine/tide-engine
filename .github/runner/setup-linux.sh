#!/bin/bash
# What the Linux runner needs that a hosted one comes with, installed once.
# Run it on the runner's machine (Ubuntu or Debian) as the user the runner
# runs as:
#
#   bash .github/runner/setup-linux.sh
#
# It asks for the sudo password where sudo needs one (packages, and the kvm
# group for Android's emulator) and shows Google's licenses for the Android SDK
# to accept; with TIDE_ACCEPT_ANDROID_LICENSE set, it accepts them.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
major=$(sed -nE 's/^set\(TIDE_LLVM_VERSION ([0-9]+)\..*/\1/p' "$here/../../cmake/LLVM.cmake")
test -n "$major"
codename=$(. /etc/os-release && echo "$VERSION_CODENAME")

# LLVM's own packages, of the major version tide's built-in clang comes from,
# and what the build and its tests use beside them.
sudo apt-get update
sudo apt-get install -y wget ca-certificates
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
echo "deb http://apt.llvm.org/$codename/ llvm-toolchain-$codename-$major main" | sudo tee /etc/apt/sources.list.d/llvm.list
sudo apt-get update
sudo apt-get install -y "clang-$major" "lld-$major" "llvm-$major" ninja-build zstd unzip cmake git curl xvfb gh \
    build-essential python3 libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev \
    openjdk-21-jdk-headless

# The browser the web tests and the docs' tests run in.
if [ ! -x /usr/bin/google-chrome ]; then
    wget -qO /tmp/google-chrome.deb https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb
    sudo apt-get install -y /tmp/google-chrome.deb
    rm /tmp/google-chrome.deb
fi

# Android's emulator needs KVM.
sudo usermod -aG kvm "$USER"

# Android's SDK: the NDK the package builds its platform layer with, the
# emulator and its image for the tests, and the tools that check an app.
sdk="$HOME/Android/Sdk"
if [ ! -x "$sdk/cmdline-tools/latest/bin/sdkmanager" ]; then
    mkdir -p "$sdk/cmdline-tools"
    wget -qO /tmp/cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    unzip -q /tmp/cmdline-tools.zip -d "$sdk/cmdline-tools"
    mv "$sdk/cmdline-tools/cmdline-tools" "$sdk/cmdline-tools/latest"
    rm /tmp/cmdline-tools.zip
fi
if [ -n "${TIDE_ACCEPT_ANDROID_LICENSE:-}" ]; then
    "$sdk/cmdline-tools/latest/bin/sdkmanager" --licenses > /dev/null < <(yes)
else
    "$sdk/cmdline-tools/latest/bin/sdkmanager" --licenses
fi
"$sdk/cmdline-tools/latest/bin/sdkmanager" --install platform-tools emulator "build-tools;36.0.0" \
    "ndk;30.0.16248370" "system-images;android-36;default;x86_64"

# Microsoft's headers and libraries, which Windows' package links its tide
# with (cmake/windows-msvc-toolchain.cmake): xwin fetches them from Microsoft
# under Visual Studio's license, which TIDE_ACCEPT_MICROSOFT_LICENSE accepts.
# Without it they're left out, and this machine builds no Windows package.
xwin_version=0.10.0
xwin_hash=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
if [ -n "${TIDE_ACCEPT_MICROSOFT_LICENSE:-}" ] && [ ! -d "$HOME/xwin/VC" ]; then
    mkdir -p "$HOME/xwin-tool"
    wget -qO "$HOME/xwin-tool/xwin.tar.gz"         "https://github.com/Jake-Shadle/xwin/releases/download/$xwin_version/xwin-$xwin_version-x86_64-unknown-linux-musl.tar.gz"
    echo "$xwin_hash  $HOME/xwin-tool/xwin.tar.gz" | sha256sum -c
    tar xzf "$HOME/xwin-tool/xwin.tar.gz" -C "$HOME/xwin-tool" --strip-components=1
    "$HOME/xwin-tool/xwin" --accept-license --cache-dir "$HOME/xwin-tool/cache" splat --use-winsysroot-style         --output "$HOME/xwin"
    rm -rf "$HOME/xwin-tool/cache" "$HOME/xwin-tool/xwin.tar.gz"
    # clang looks for the libraries of x64 by that name.
    for dir in "$HOME"/xwin/VC/Tools/MSVC/*/lib "$HOME/xwin/Windows Kits/10/Lib"/*/um "$HOME/xwin/Windows Kits/10/Lib"/*/ucrt; do
        [ -e "$dir/x64" ] || ln -s x86_64 "$dir/x64"
    done
fi

# Jobs find the SDK through the runner's environment.
env="$HOME/tide-runner/.env"
if [ -d "$HOME/tide-runner" ] && ! grep -q '^ANDROID_HOME=' "$env" 2>/dev/null; then
    echo "ANDROID_HOME=$sdk" >> "$env"
fi

echo "Done. Start the runner again, so it's in the kvm group and has ANDROID_HOME."
