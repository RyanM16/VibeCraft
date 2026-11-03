#!/usr/bin/env bash
# Source this only in the managed Linux cloud environment. It selects software Vulkan by default.
export VC_SDK_ROOT=/workspace/.vibecraft-sdk/root
export PATH="$VC_SDK_ROOT/usr/bin:$PATH"
export LD_LIBRARY_PATH="$VC_SDK_ROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="$VC_SDK_ROOT/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export VK_LAYER_PATH="$VC_SDK_ROOT/usr/share/vulkan/explicit_layer.d${VK_LAYER_PATH:+:$VK_LAYER_PATH}"
if [[ "${VC_SOFTWARE_VULKAN:-1}" == 1 ]]; then
  export VK_DRIVER_FILES="$VC_SDK_ROOT/usr/share/vulkan/icd.d/lvp_icd.json"
fi
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/workspace/.vibecraft-sdk/runtime}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.vibecraft-sdk/cache}"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CACHE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
