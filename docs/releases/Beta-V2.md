# VibeCraft Beta V2

Source prerelease `beta-v2` for the first playable Vulkan sandbox. Build and launch instructions, controls, and Vulkan requirements are in the [README](../../README.md).

- First-person walking, jumping, sprinting, mouse look, and collision against current voxels.
- Raycast mining and placement, an amber target reticle, and a check that prevents placing a block inside the player.
- A bounded rolling terrain window with camera-relative geometry. One worker builds a replacement mesh, and the game rejects results built across an edit or for a no-longer-relevant window.
- Seed and world-file selection, background F5 snapshots, and a final save on clean exit. Existing Beta V1 edits use the same snapshot format.
- Window resizing, live player and terrain diagnostics, and the existing headless benchmark scenarios.

## Scope and defaults

The engineering spec’s `VC-ALPHA-0` gameplay loop set the scope for this beta. The 8-block interaction range, flat-color palette, grass placement material, movement tuning, spawn column, and rolling radius are developer defaults. Player position resets on startup. Save data uses an atomic world snapshot rather than per-region journal records. Playable terrain uses a local voxel window. Coarse distant terrain, including the 2,096-chunk experiment from V1, remains available in benchmark mode.

## Validation

Linux release CTest passed the core tests, the streaming smoke test, and the offscreen Vulkan validation smoke test. The 120-frame stress suite passed 12 CPU and Vulkan workloads. The core and streaming smoke tests also passed with AddressSanitizer and UndefinedBehaviorSanitizer. The windowed test used Xvfb and Mesa llvmpipe. Windows Vulkan and dedicated GPU performance remain unverified.

| Spec check | Evidence |
|---|---|
| T-001 generator determinism | Existing fixed-seed determinism and chunk source checks passed. The fresh-process window reopen used the same seed. |
| T-002 coordinate indexing | Existing negative and large coordinate checks passed. |
| T-003 boundary edits | CPU tests verified neighbor meshing and collision changes at a brick boundary. Visually inspecting an edit directly on a seam remains open. |
| T-004 save and reload | Existing round-trip tests passed. The window test mined a block, placed a block, saved, reopened, and acquired the placed block as its current raycast target. |
| T-005 interrupted or corrupt save | The corruption and abandoned temporary snapshot checks passed. Forced interruption during disk writes remains open. |
| T-006 stale work | Existing streaming cancellation and edit invalidation tests passed. Play mode also rejects jobs whose edit revision or center is stale. |
| T-007 Vulkan lifecycle | Windowed play resized from 640×360 to 880×510 and exited with zero Vulkan validation errors. Device-loss injection remains open. |
| T-008 benchmark | Existing software Vulkan and CPU smoke paths passed. Gameplay benchmark targets on physical hardware remain unmeasured. |
| T-009 gameplay | Windowed movement, jumping, mining, placement, targeting, and save/reload were exercised. The collision regression test verifies a wall blocks movement until its voxels are mined. |
