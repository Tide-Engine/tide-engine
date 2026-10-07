#!/bin/bash
# What the Linux runner needs that a hosted one comes with, installed once.
# Run it in the runner's WSL as the user the runner runs as:
#
#   bash .github/runner/setup-linux.sh
#
# It asks for the sudo password (packages, and the kvm group for Android's
# emulator) and shows Google's licenses for the Android SDK to accept.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
major=$(sed -nE 's/^set\(TIDE_LLVM_VERSION ([0-9]+)\..*/\1/p' "$here/../../cmake/LLVM.cmake")
test -n "$major"
codename=$(lsb_release -cs)

# LLVM's own packages, of the major version tide's built-in clang comes from,
# and what the build and its tests use beside them.
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc > /dev/null
echo "deb http://apt.llvm.org/$codename/ llvm-toolchain-$codename-$major main" | sudo tee /etc/apt/sources.list.d/llvm.list
sudo apt-get update
sudo apt-get install -y "clang-$major" "lld-$major" "llvm-$major" ninja-build zstd unzip cmake git curl xvfb \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev openjdk-21-jdk-headless

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
"$sdk/cmdline-tools/latest/bin/sdkmanager" --licenses
"$sdk/cmdline-tools/latest/bin/sdkmanager" --install platform-tools emulator "build-tools;36.0.0" \
    "ndk;30.0.16248370" "system-images;android-36;default;x86_64"

# Jobs find the SDK through the runner's environment.
env="$HOME/tide-runner/.env"
if [ -d "$HOME/tide-runner" ] && ! grep -q '^ANDROID_HOME=' "$env" 2>/dev/null; then
    echo "ANDROID_HOME=$sdk" >> "$env"
fi

echo "Done. Start the runner again, so it's in the kvm group and has ANDROID_HOME."
