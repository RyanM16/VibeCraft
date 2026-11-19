#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
sdk=/workspace/.vibecraft-sdk
root="$sdk/root"
if [[ "$(uname -m)" != x86_64 ]]; then
  echo 'This local Debian SDK installer supports x86_64 only. Use system dependencies on other platforms.' >&2
  exit 1
fi
if [[ "${VC_REFRESH_SDK:-0}" == 1 || ! -x "$root/usr/bin/cmake" || ! -x "$root/usr/bin/glslangValidator" || ! -x "$root/usr/bin/clang-format-19" || ! -f "$root/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so" ]]; then
  mkdir -p "$sdk/apt/lists/partial" "$sdk/apt/archives/partial" "$root"
  cat > "$sdk/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
  cat > "$sdk/apt/apt.conf" <<'APTCONF'
Dir::Etc::sourcelist "/workspace/.vibecraft-sdk/apt/sources.list";
Dir::Etc::sourceparts "-";
Dir::Etc::parts "-";
Dir::State::lists "/workspace/.vibecraft-sdk/apt/lists";
Dir::Cache::archives "/workspace/.vibecraft-sdk/apt/archives";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
Debug::NoLocking "true";
APT::Get::List-Cleanup "false";
Acquire::Languages "none";
APTCONF
  # Preserve repository signatures, package hashes, TLS verification, and the session's proxy.
  APT_CONFIG="$sdk/apt/apt.conf" /usr/bin/apt-get -o "Acquire::https::Proxy=${HTTPS_PROXY:?managed HTTPS proxy required}" update
  APT_CONFIG="$sdk/apt/apt.conf" /usr/bin/apt-get -o "Acquire::https::Proxy=$HTTPS_PROXY" --download-only --yes --no-install-recommends --reinstall install \
    cmake ninja-build libvulkan-dev vulkan-tools vulkan-validationlayers mesa-vulkan-drivers \
    libglfw3-dev glslang-tools xvfb xauth libzstd-dev clang-format-19
  for package in "$sdk"/apt/archives/*.deb; do dpkg-deb -x "$package" "$root"; done
fi
source tools/cloud-env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVC_GPU_TESTS=ON \
  -DVulkan_INCLUDE_DIR="$root/usr/include" -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
