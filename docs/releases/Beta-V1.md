# Beta V1

Initial C++20/Vulkan 1.3 voxel-engine test bed, released as Git tag `beta-v1`.

Includes deterministic 16³/32³ voxel terrain, greedy meshing, block edits and checksummed persistence, ray/collision queries, bounded CPU streaming experiments, a Vulkan viewer, coarse far-surface rings, telemetry, stress harnesses, and separate 4K RTX 3080 / 1440p RX 9060 XT hardware profiles.

Validation: 12 core tests, 24 stress workloads, windowed and offscreen Vulkan checks with zero final validation errors, and ASan/UBSan/leak checks passed in the cloud environment. Release CTest was rerun successfully before the GitHub push. Details and raw baseline data are linked from [benchmark notes](../performance/benchmarks.md).

This is a preliminary laboratory. Continuous viewer streaming, volumetric mid LOD, seamless transitions, edit-aware distant terrain, and advanced lighting are unfinished. Continuous edits can repeatedly cancel 32³ jobs; recovery after edits stop is tested. Windows and actual NVIDIA/AMD performance remain unverified. No 100/200 FPS claim is made.

Build, controls, and target-PC benchmark commands: [README](../../README.md).
