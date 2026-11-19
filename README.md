# VibeCraft — Beta V2

A native C++20/Vulkan 1.3 single-player voxel sandbox. Walk and jump across seeded terrain, mine and place blocks, and reopen the same world to resume your edits. The original free-flight and performance workloads are available through the benchmark mode. See the [Beta V2 release notes](docs/releases/Beta-V2.md) and [engine architecture](docs/architecture.md).

## Launch

After [building on Linux](#build-on-a-normal-linux-desktop) or [building on Windows](#windows-development), launch `vibecraft` from the build directory, or run it from the repository root as shown below. The world path is relative to your working directory.

```bash
./build/vibecraft --seed 42 --world worlds/my-world.vced
```

Seed 42 and `worlds/testbed.vced` are the defaults. For a world created with a custom seed, use the same `--seed` each time you open its file. Beta V1 edit snapshots can be opened with seed 42. The game restores edits and spawns you near the origin.

### Controls

| Action | Control |
|---|---|
| Walk and strafe | W A S D |
| Look | Mouse or arrow keys |
| Jump and sprint | Space and Left Shift |
| Mine and place a grass block | Left/right click or E/Q, within 8 blocks |
| Return to spawn | R |
| Release or capture the pointer | Tab |
| Save | F5, also on clean exit |
| Quit | Esc |

An amber crosshair marks a block within reach. The window title reports the target position, seed, FPS, frame time, player coordinates, brick count, mesh memory, and pending terrain jobs. `--radius 2..4` sets the playable window's horizontal half-width in 16-block chunks. The default is 3, or 48 blocks from its snapped center.

### Run in the managed Linux cloud

```bash
bash tools/setup-cloud.sh
source tools/cloud-env.sh
build/vibecraft --validation
```

The setup script installs a signed Debian SDK under `/workspace/.vibecraft-sdk`, builds the game, and runs CTest. It uses a user-local SDK without requiring root. `cloud-env.sh` defaults to Mesa software Vulkan. For a display-free run, use a benchmark command below.

## Stress tests

```bash
ctest --test-dir build --output-on-failure
python3 tools/stress.py --frames 600             # 8 CPU + 4 Vulkan workloads
python3 tools/stress.py --frames 600 --full      # also 1080p, 1440p, 4K
build/vc_bench --scenario teleport --side 32 --budget-mib 1 --frames 1200
build/vibecraft --headless --validation --frames 300 --scenario edits --width 1920 --height 1080
```

Each stress run creates a fresh `artifacts/stress-.../` directory with logs, screenshots, CSVs, JSONs and a summary. Commands fail on validation errors, failed assertions, timeouts, output failures, or budget violations. Screenshots are inspected for actual terrain and sky, not just the existence of an image. `--scenario edits` uses a persistent test-world file; pass `--world PATH` to isolate your save.

The free-flight viewer is available with `--scenario stationary`. Benchmark options select the legacy coarse terrain-ring experiment. Its 33,536-block axis extent measures the reach of approximate visual geometry.

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

The Windows game executable still needs a native Vulkan smoke test. The cloud software driver cannot validate RTX 3080 performance. The supplied hardware targets are recorded in `config/hardware_profiles.json`; production budgets still require measurements. This test bed makes no 100/200 FPS claim.

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

## Beta limits

The playable view uses a rolling window of exact voxels and five vertical brick layers. A single background job rebuilds its mesh after movement or edits. Collisions use world data immediately, and the corresponding geometry updates on the next accepted upload. The window can show a terrain edge, and repeated edits can defer the visible refresh. The save records the seed and block edits in a single atomic snapshot. Each save file expects one running game instance.

Distant gameplay terrain LOD, region journaling, advanced lighting, and the later gameplay systems remain on the roadmap. See the [V1 engine decisions](docs/decisions/ADR-0001-testbed.md), [V1 release notes](docs/releases/Beta-V1.md), and [V2 validation](docs/releases/Beta-V2.md).
