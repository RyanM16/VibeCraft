# VibeCraft — Beta V1

A C++20/Vulkan 1.3 laboratory for testing the proposed voxel engine before building a full game. The architecture proposal is preserved in [docs/architecture.md](docs/architecture.md). This implementation is an early P0/P1 experiment with isolated P2 streaming tests, not completion of the six engine milestones.

## Run in this cloud environment

```bash
cd /workspace/VibeCraft
bash tools/setup-cloud.sh
source tools/cloud-env.sh
build/vibecraft --headless --validation --frames 120 --scenario flight \
  --width 1280 --height 720 --screenshot artifacts/terrain.ppm
python3 tools/stress.py --frames 600
```

`setup-cloud.sh` installs a user-local, signature-verified Debian SDK when needed, builds the project, and runs CTest. It needs outbound package access only when installing or refreshing dependencies. It does not require root. The SDK lives outside the checkout at `/workspace/.vibecraft-sdk`. Set `VC_REFRESH_SDK=1` to refresh it. `cloud-env.sh` selects Mesa software Vulkan by default; it is intended for this cloud machine, not your gaming PC. Headless runs must specify a finite `--frames` value.

For an interactive desktop, run `build/vibecraft --validation`. This cloud task has no user-facing desktop preview. The window path was tested internally with Xvfb. Local X11 sockets may require running the virtual-display check outside the command sandbox.

## Controls

| Action | Control |
|---|---|
| Fly / strafe | W A S D |
| Look | Arrow keys |
| Ascend / descend | Space / Left Ctrl |
| Fly faster | Left Shift |
| Remove / place a grass block | E / Q, within 12 blocks of the view center |
| Save edits | F5 (also saved on clean exit) |
| Quit | Esc |

The camera is a free-fly inspection camera. Collision and ray queries are tested engine APIs; the camera does not yet use walking physics. The exact editable patch stays at world origin. Moving beyond it shows the coarse surface experiment, not streamed editable chunks. An edit currently rebuilds this patch synchronously; edit-stall timings are intentionally visible in render telemetry.

## What works

- Seeded, versioned integer-coordinate terrain with simple caves, 16³/32³ generation, and a shared source query.
- Neighbor-aware greedy opaque voxel meshes; exact ray edits and AABB collision queries.
- Versioned edit snapshots, deterministic ordering, corruption checks, seed validation, transactional loading, data flush and atomic replacement. Invalid saves fail visibly instead of being silently discarded.
- Bounded prioritized workers, request deduplication, cancellation epochs, edit invalidation, LRU mesh eviction, upload-admission budgets, and an independent metadata-count cap. These are exercised by the **CPU streaming laboratory**, not yet connected to continuous viewer streaming.
- Vulkan 1.3 feature detection, GLFW windows/swapchain or display-free rendering, HLSL terrain shaders, depth testing, fog, explicit pass barriers, GPU timestamps, screenshot readback, and synchronization validation.
- A coarse surface-ring experiment reaching **33,536 blocks per horizontal axis from origin** (2,096 chunks). It samples surfaces rather than loading millions of chunks. The square corners extend farther than the stated axis distance.
- CPU CSV/JSON benchmarks and rendered frame telemetry: median/p95/p99, slowest-1% mean converted to FPS, GPU opaque-pass times, mesh capacity, peak process RSS, queue and cache counters.

## Stress tests

```bash
ctest --test-dir build --output-on-failure
python3 tools/stress.py --frames 600             # 8 CPU + 4 Vulkan workloads
python3 tools/stress.py --frames 600 --full      # also 1080p, 1440p, 4K
build/vc_bench --scenario teleport --side 32 --budget-mib 1 --frames 1200
build/vibecraft --headless --validation --frames 300 --scenario edits --width 1920 --height 1080
```

Each stress run creates a fresh `artifacts/stress-.../` directory with logs, screenshots, CSVs, JSONs and a summary. Commands fail on validation errors, failed assertions, timeouts, output failures, or budget violations. Screenshots are inspected for actual terrain and sky, not just the existence of an image. `--scenario edits` uses a persistent test-world file; pass `--world PATH` to isolate your save.

CPU streaming workloads simulate a 2 ms frame interval and **do not measure rendered FPS**. Renderer telemetry excludes startup/initial mesh construction and its first five warmup frames; input, edit rebuilds, fence waits and submission are included after warmup. GPU timestamps measure the opaque pass, not presentation, frame transfers or total GPU work. Windowed presentation uses FIFO/vsync and one frame in flight. `--headless` avoids presentation; screenshots copy once after the measured run. See [measurement notes](docs/performance/benchmarks.md).

## Build on a normal Linux desktop

Install a C++20 compiler, CMake ≥3.25, Ninja, Vulkan ≥1.3 loader/headers/driver, GLFW ≥3.3, Vulkan validation layers, and DXC or glslangValidator with HLSL support. On Debian/Ubuntu, corresponding development packages include `libvulkan-dev libglfw3-dev glslang-tools vulkan-validationlayers mesa-vulkan-drivers ninja-build cmake`.

```bash
cmake --preset release -DVC_GPU_TESTS=ON
cmake --build --preset release
ctest --preset release
./build/vibecraft --validation
```

CMake prefers `dxc` when available; the cloud-tested fallback compiles the same HLSL with glslang. No advanced shaders or DXC-specific functionality are assumed. CPU-only tests have no graphics dependencies:

```bash
cmake --preset cpu
cmake --build --preset cpu
ctest --preset cpu
cmake --preset sanitize       # GCC/Clang: ASan + UBSan
cmake --build --preset sanitize
ctest --preset sanitize
```

## Windows development

Use Visual Studio 2022 C++ tools, CMake ≥3.25, Ninja, a Vulkan 1.3-capable graphics driver, and the LunarG Vulkan SDK (including validation and `dxc` or `glslangValidator`). For GLFW, use vcpkg with the included manifest. From a developer PowerShell, with `VCPKG_ROOT` pointing to your vcpkg checkout:

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-win --parallel 4
ctest --test-dir build-win --output-on-failure
.\build-win\vibecraft.exe --validation
python tools/stress.py --build build-win --frames 600 --full
```

Windows commands and CI are provided but have not been executed in this Linux task. The cloud software driver cannot validate RTX 3080 performance. The supplied hardware targets are recorded in `config/hardware_profiles.json`; production budgets still require measurements. This test bed makes no 100/200 FPS claim.

## Your hardware profiles

| Profile | Display | GPU / VRAM | System RAM | OS |
|---|---|---|---|---|
| `rtx3080-4k` | 3840×2160 | RTX 3080 / 10 GB | 32 GB DDR5 | Windows 11 |
| `rx9060xt-1440p` | 2560×1440 | RX 9060 XT / 16 GB | 64 GB | Not specified |

Run on the corresponding PC, using its local build directory:

```powershell
python tools/stress.py --build build-win --profile rtx3080-4k --frames 1200 --render-frames 600
python tools/stress.py --build build-win --profile rx9060xt-1440p --frames 1200 --render-frames 600
```

Profiles select the resolution and record your target hardware alongside the **actual** Vulkan device. They do not substitute a hardware name into measurements or claim the cloud GPU matches it. Both retain a deliberately small 1 MiB CPU mesh-cache cap for eviction stress, not a recommended gameplay allocation. The initial flat-color/fog preset and exact near radius are held equal for comparison. `--render-frames` controls rendered samples; `--frames` controls CPU workloads. Run on an otherwise idle PC and retain logs/driver details when comparing results.

## Deliberately unfinished

Volumetric mid LOD, edit-aware far summaries, seamless geomorphing, endless viewer streaming/rebasing, palette compression, region journaling/recovery/migration, work stealing, GPU indirect culling, a general frame graph, Tracy integration, pipeline caching, advanced materials/shadows/water/TAA, animation, Jolt and gameplay are future milestones. The current surface rings use skirts to mask gaps; they can pop, miss caves/structures, and ignore distant edits. The fog/flat-color scene is a correctness baseline, not the final quality preset. RAM cache caps count mesh vector capacities; metadata is separately count-bounded and process RSS includes other allocations. There is no measured VRAM high-water telemetry yet.

See [implementation decisions and next gates](docs/decisions/ADR-0001-testbed.md). Beta V1 is the initial test-bed baseline. See [release notes](docs/releases/Beta-V1.md) for its tested scope and limitations.
