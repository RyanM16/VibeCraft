# VibeCraft — Engine Architecture & Technical Design

**Version:** 0.1 | **Date:** 2026-11-02 | **Status:** Concept / proposed design, not yet implemented or benchmarked

## 0. Executive brief
Build a Minecraft-like, editable, procedurally generated voxel game with a visually continuous horizon equivalent to **2,096 Minecraft chunks** (33,536 blocks / ~33.5 km assuming 1 block = 1 m). The intent is **extremely high visual fidelity and nearly imperceptible LOD transitions**, with advanced shaders, complex animation, and support for 1080p, 1440p, and 4K. **100 FPS is the first performance milestone; 200+ FPS is the aspirational stretch target.** A 100 FPS frame budget is 10 ms; 200 FPS is 5 ms. Performance targets are hardware, resolution, quality preset, and workload-dependent—not promises.

**Critical distinction:** Render distance refers to *visible world extent*, not simulation distance, loaded full-resolution chunks, or a guarantee that all individual blocks remain distinguishable. At this horizon, far terrain must be approximated. At 2,096 chunks the horizontal circle has about 13.8 million chunk columns; a square footprint includes (2×2,096+1)^2 = 17,581,249 chunk columns. Neither set should be loaded as full-resolution block data.

## 1. Goals, measurable acceptance criteria and assumptions

### Requirements
- Smooth exploration in every direction of a very large deterministic world; no visible chunk seams, mesh cracks, or severe generation stalls.
- Nearby terrain is destructible/buildable at exact block resolution; changed world data persists reliably.
- Shadows, water, atmospheric scattering/fog, modern materials, reflections, and selective indirect lighting.
- Near-invisible geometric/texture/lighting transitions as the camera moves, including aerial flight and movement at speed.
- Modern entity animation and physics; NPC simulation distance can be far smaller than visible landscape distance.
- Windows-first development, designed so Linux can be supported without replacing the renderer.
- Hardware baseline for initial testing: Ryzen 7 7800X3D + RTX 3080 (provided earlier by user); test across resolution and graphics settings. System RAM/VRAM capacities must be confirmed rather than inferred.

### Performance ladder
1. **Milestone A:** 100 FPS in a repeatable 1080p test world at a defined quality preset and 2,096-chunk *visual* horizon.
2. **Milestone B:** Stable 100 FPS at 1440p, then improve 4K output using temporal upscaling where necessary.
3. **Stretch:** 200+ FPS with shaders at one or more defined resolutions/presets; only claim after benchmark data.
4. Track frame-time median, p95, p99 and 1% lows, CPU render/simulation time, GPU pass time, streaming stalls, VRAM/RAM high-water marks, generation latency, geometry counts, draw commands, and visible LOD error.

## 2. Proposed development stack
- **Engine:** C++20/23, CMake, clang-cl/MSVC for Windows, sanitizer builds where available, automated test runner.
- **Rendering:** Vulkan 1.3 core baseline with feature detection; optional extension paths for advanced GPU features. Avoid assuming mesh shaders, sparse residency, or hardware ray tracing are universally available.
- **Shaders:** HLSL compiled to SPIR-V using DXC; shader permutations constrained; persistent pipeline cache and offline compilation.
- **OS/window/input:** SDL3 or GLFW (select after prototype); raw input where appropriate.
- **Job system:** work-stealing thread pool with priority queues; explicit memory budgets and cancellation tokens.
- **Data encoding:** own packed voxel/chunk formats and checksummed region persistence; zstd compression where beneficial.
- **Procedural terrain:** deterministic, versioned noise/biome/erosion pipeline (FastNoise2 optional); coordinate-stable at every LOD.
- **Physics:** initially custom voxel AABB/raycast collision + Jolt for dynamic rigid bodies.
- **Animations:** glTF import, hierarchical skeletal animations, blending, GPU skinning, animation LOD.
- **Tooling:** Tracy (CPU profiling), RenderDoc (frame debugging), Vulkan validation layers; GPU timestamps and vendor profiler where available.

## 3. Non-negotiable architectural boundaries
Modules: `platform`, `core`, `task_scheduler`, `worldgen`, `world_storage`, `voxel_near`, `terrain_lod`, `streaming`, `mesher`, `renderer_vk`, `materials`, `lighting`, `physics`, `entities`, `animation`, `networking_later`, `tools`, `tests`.

**Source of truth** is a deterministic world definition plus persistent edits. The render representations are disposable caches. All terrain representations must derive from the *same* world height/material/feature functions and incorporate persistent edits at their appropriate resolution. Separate render data from mutable gameplay state.

Coordinates: `int64` block/world indexing; double-precision CPU camera/world transforms for long range where needed; GPU camera-relative/local float coordinates. Rebase entities around the camera rather than render 33 km geometry using imprecise global floating-point positions.

## 4. Terrain representations: three-tier hybrid

### Tier 1: exact near-field blocks (experiment: 0–16 chunks)
- Editable, authoritative block storage, chunk subdivision initially compare 16³ and 32³, measured independently of mesh partition size.
- Palette-compressed block identifiers; optional sparse air storage; bit packing; dirty-region tracking.
- Hidden-face elimination + greedy merging for compatible opaque surfaces; separate transparent/translucent passes; material texture arrays/atlases with gutter handling and mipmaps.
- Neighbour-aware face generation; asynchronous local remesh on edits; queue updates with limited upload budget/frame.
- Exact voxel collision and accurate game interaction; dynamic entities and high-quality lighting.

### Tier 2: mid-field volumetric LOD (experiment: 16–256 chunks)
- Hierarchical grid/octree or clipmap with coarser samples and conservative occupancy/aggregate material metadata.
- Preserve large caves, overhangs, arches, cliff silhouettes and major authored/edited structures; pure heightmaps are inadequate here.
- Mesh simplified solid surfaces, choosing LOD via projected screen-space error, view velocity, silhouette sensitivity and previous-frame choice.
- Cache simplified meshlets or geometry groups; update only relevant regions when edits occur. Test volumetric occupancy/dual-contour-like surface extraction versus aggregate greedy voxel meshes (not a premature commitment).

### Tier 3: far-field surface/landmark LOD (experiment: 256–2,096 chunks)
- Heightfield/geometry clipmaps or quadtree terrain tiles at very low sampling density, with low-cost materials, silhouettes and landmark proxy meshes.
- Directly sample deterministic procedural generation for distant tiles rather than generating hidden caves, ores, foliage, entities, or individual blocks.
- Far-field sky/atmosphere, aerial perspective, horizon water and terrain shadows approximated cheaply; trees become canopy patches/impactors, large structures become impostors/proxies where appropriate.
- Incorporate high-impact player changes through an edit-aware coarse-layer summary; do not silently revert huge alterations when viewed from a distance.

**LOD radii are provisional.** Transition based on pixel error and budgets; revise after measurement, especially for flight, vertical terrain, and 4K.

## 5. Nearly imperceptible LOD changes
1. Generate child and parent LOD from shared source functions; consistent normals/material IDs and edge boundaries.
2. **Geomorph** vertices between LODs within overlapping rings; use crack-free stitching/skirt/fan where appropriate (skirt alone does not fix silhouette popping).
3. **Crossfade** material/normal/coverage in the transition band via noise/blue-noise dithering + temporal AA; manage depth, motion vectors and transparency to avoid ghosting.
4. **Hysteresis** around LOD thresholds to prevent flicker; prefetch next LOD before transition begins.
5. Preserve screen-space silhouette error, especially mountain ridge edges and tall structures; allow more geometry there than on flat ground.
6. Shadow LOD must coordinate with visible terrain LOD; fade shadow detail separately so objects do not visibly lose shadows first.
7. Camera fly-through regression tests (ground, flight, rapid yaw, sunrise/sunset, teleport, abrupt build/destroy) with image difference / temporal flicker checks.

## 6. Vulkan renderer and frame graph
**Initial path:** forward+ or deferred? Prototype both against voxel material/lighting workloads; likely deferred/hybrid for many lights, forward specialization for water/transparency. Build a frame graph for pass dependencies, resource lifetime, transient aliasing, and synchronization.

Pipeline concept:
1. Collect per-region bounding info and coarse residency tables.
2. GPU compute frustum + previous-frame depth-pyramid occlusion culling; classify visible near geometry, mid LOD, far tiles. Avoid occlusion over-culling around camera moves; use conservative fallback.
3. Select LOD, gather material and geometry references; compact draw metadata and generate indirect draw commands.
4. Depth prepass as profiling warrants; depth pyramid for next frame.
5. Terrain opaque pass, entities/animated geometry, shadow cascades/virtual-like shadow options (only if measured), water/translucent pass.
6. Screen-space AO / appropriate GI, atmospheric scattering, volumetrics, reflections, postprocessing, temporal AA, optional DLSS/FSR/XeSS integrations after baseline correctness.
7. Instrument every pass with GPU timestamps.

Start with indexed indirect drawing and GPU frustum culling; treat mesh shaders, bindless, sparse textures, and ray tracing as conditional optimizations requiring real measurements. Multi-draw indirect reduces draw submission overhead, but it does not make enormous geometry/pixel shading free.

## 7. Lighting architecture by distance
- **Near:** physically based shading, directional cascaded shadows / other shadow strategies, local point lights with clustered/forward+ lists, screen-space AO, higher quality water and reflections, optional GI.
- **Mid:** stable shared sun/sky lighting; lower shadow density, reduced ambient detail, simplified reflections; major structure shadows preserved when visually important.
- **Far:** terrain albedo, normals and sky illumination, atmospheric perspective, simplified large-scale self-shadows or precomputed/dynamic low-frequency shadowing. Avoid full-rate per-fragment expensive ray effects across 33 km.
- Temporal reconstruction/denoising needs motion vectors and robust handling of disocclusions and chunk/LOD changes. Expose render resolution scale and distinct 1080p/1440p/4K quality presets.

## 8. CPU utilization, tasks, synchronization
- Dedicated game/simulation scheduling separate from asynchronous streaming and heavy generation; avoid blocking the main/render thread on disk or world generation.
- Work-stealing worker pool with explicit priority: imminent-visible + collision > mid LOD > far prefetch > cache maintenance.
- Bounded queues and cancellation/versioned requests prevent wasted work when the camera teleports.
- Each job owns temporary scratch arenas; minimize per-voxel allocations and false sharing; use structure-of-arrays for hot loops.
- Batch adjacent edits and mesh uploads. Double/triple-buffer command and staging resources with lifetime tracking; no per-frame `deviceWaitIdle`.
- Simulation and animation LOD distinct from visibility LOD: distant world visible ≠ distant mobs actively simulated.

## 9. RAM, VRAM, disk — preliminary budgets (NOT measured)
Use adjustable *caps*, not a design that reserves memory based on total visible chunk columns.

**Example memory targets for a 32 GB RAM / 10–12 GB VRAM-class test PC (must confirm actual available VRAM):**
- CPU near-field voxel working set: ~0.5–1.5 GiB.
- CPU LOD tile/mesh caches: ~1–3 GiB.
- Streaming + decompression + staging: ~0.5–1.5 GiB.
- GPU near geometry + indirect draw buffers: ~0.5–1.5 GiB.
- GPU mid/far mesh/LOD textures: ~0.5–1.5 GiB.
- GPU material textures: ~1–2 GiB.
- GPU frame targets, temporal history, lighting/shadow buffers: ~1–3+ GiB depending strongly on 4K and effects.
- Remainder: game entities, OS, graphics driver, fragmentation, transient workloads and other software.

These are **illustrative adjustable ceilings**, not allocations to immediately create and not guarantees they fit a specific RTX 3080 SKU. Implement telemetry plus pressure-driven eviction: distant/coarse data stays cheap; near critical player-collision data takes priority. Use LRU/clock approximations weighted by distance, reload cost and GPU use. Compressed on-disk region cache with version/checksum and atomic save strategy; validate crash recovery and schema migration.

**Back-of-envelope:** storing 17.58M square-footprint chunk columns at just 64 bytes metadata each would cost ~1.05 GiB before geometry, terrain, material data or traversal overhead. Keep indexing hierarchical rather than materializing one object for every distant chunk column.

## 10. World generation, fidelity and editing
- Seed + versioned deterministic hierarchy: continental landform → biome/climate → macro erosion/rivers → local terrain/caves/ores → foliage/structures.
- A height/surface query API that runs at multiple spatial scales; expensive underground generation only for near-active exact blocks.
- Changes as compact per-region delta layers over seed-generated baseline; persist edits with journal/atomic region swap and checksums.
- Ensure cached far proxies react to major edits; event invalidation and bounded recompute queues. Test consistency at terrain borders and across version upgrades.
- Distinguish full-height vertical chunks from chunk *columns* in metrics and storage.

## 11. 200 FPS stretch strategy / bottleneck decision tree
A 5 ms/frame target is extreme with heavy shaders; 100 FPS allows 10 ms. Do not simply sum independent CPU and GPU timings because they can overlap. Investigate:
- GPU fragment bottleneck: reduce internal resolution, shader samples, costly transparency/reflections, volumetric steps, overdraw, material variation; temporal upscaling.
- GPU vertex/geometry bottleneck: stronger LOD, mesh compaction, silhouette-aware simplification, culling, meshlet-like grouping.
- CPU render-thread bottleneck: more GPU-driven culling/indirect draw, fewer state changes, batch uploads, prebuilt command work.
- Generation/streaming stalls: asynchronous jobs, visibility-driven scheduling, fast cache, predictive prefetch, bounded IO/uploads.
- Memory pressure: lower cache budgets, better compression, residency-aware resource management.
- Shader/Lod temporal noise: tighten motion vectors, history rejection, morph bands, dither stability.

**Design principle:** no 200 FPS claim before scene-specific CPU/GPU traces, at the chosen output resolution and rendering settings, including fast flight while new world regions are generated.

## 12. Development milestones and exit gates
**P0 — Instrumented skeleton:** CMake engine, Vulkan swapchain, input, frame graph prototype, Tracy and GPU timestamps. Exit: stable high-FPS empty scene, clean validation.

**P1 — Editable block world:** deterministic procedural near terrain, chunk loading, greedy meshing, raycast edit, collision, region saves. Exit: persistent edits and stable frametimes during walking.

**P2 — Streaming + threads:** prioritized job queues, generation, caches, uploads, cancellation. Exit: flight/teleport stress tests without blocking frame rendering.

**P3 — Hierarchical LOD:** volumetric mid + surface far, 2,096-chunk visual horizon, LOD consistency. Exit: no obvious cracks/pops in movement/flight capture; fixed RAM/VRAM budget.

**P4 — Advanced rendering:** sun/sky, shadows, high-quality water, materials, atmospheric effects, temporal reconstruction, optional GI. Exit: measure 100 FPS at defined resolution/hardware/scene; optimize p99 spikes.

**P5 — Game foundations:** entity component design, animation blending, AI simulation bounds, Jolt rigid bodies, world systems, save/version tooling.

**P6 — 200 FPS stretch:** optimize only measured bottlenecks, establish specific quality presets and supported GPU tiers; evaluate high-FPS vs 4K tradeoffs.

## 13. Repository structure (suggested)
```
VibeCraft/
  CMakeLists.txt
  docs/architecture.md
  docs/decisions/ADR-0001-renderer.md
  docs/performance/benchmarks.md
  engine/core/
  engine/platform/
  engine/render/vulkan/
  engine/world/generator/
  engine/world/storage/
  engine/world/voxel/
  engine/world/lod/
  engine/world/streaming/
  engine/physics/
  engine/animation/
  game/
  assets/shaders/
  tools/
  tests/
  benchmarks/
```

## 14. Open decisions (record rather than assume)
1. Exact shader quality reference: stylized high-end vs near-photorealism; real-time GI vs hybrid RT.
2. Baseline resolution/quality at 100 FPS, and separate 200 FPS stretch quality preset.
3. Actual GPU memory size, system RAM, OS and target minimum hardware.
4. Singleplayer-first vs multiplayer-first; networking strongly affects authoritative world/simulation.
5. Full 3D overhang/volumetric fidelity requirement at extreme far distances; silhouette metrics.
6. Camera speed and flying/teleport support; expected max velocity drives prefetch budget.
7. Height range, block sizes, map topology, water coverage, biome complexity, cave density.
8. Chunk storage/granularity and LOD tile sizes to settle empirically, not from habit.
9. Modding/shader-pack API, scripting support and compatibility aims.
10. Long-term portability to DirectX 12/Metal; avoid API coupling in world/simulation code.

## 15. Risks and mitigation
- **Horizon-size misunderstanding:** do not promise 2,096 chunks of exact cubes; communicate LOD-based visual distance.
- **LOD mismatch and pop:** one hierarchical generator, parent-child consistency, morphing + fade + temporal tests.
- **Procedural generation expense:** query coarser distant fields directly; never generate every distant underground voxel.
- **VRAM oversubscription:** strict residency caps and eviction policy.
- **4K high-end shader cost:** decouple output from render resolution, scalable lighting and hybrid RT toggles.
- **Premature GPU feature reliance:** fallbacks for unsupported optional Vulkan extensions.
- **Custom engine time cost:** playable vertical slice before ambitious tooling or optimization branches.

## 16. External reference points
- Khronos Vulkan GPU-driven Multi-Draw Indirect sample: https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html
- Khronos Vulkan sparse resource spec: https://docs.vulkan.org/spec/latest/chapters/sparsemem.html
- GPU Gems 2 — GPU-Based Geometry Clipmaps and transition morphing: https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry
- Vulkan shader stages: https://docs.vulkan.org/spec/latest/chapters/shaders.html

## 17. Design log
- **2026-11-02:** C++ + Vulkan preferred custom-engine baseline. Visual horizon = 2,096 Minecraft chunks, extreme fidelity via hybrid near/mid/far LOD. Target 100 FPS initial, 200+ FPS stretch; 1080p/1440p/4K output support. No implementation yet.

**Next action:** Implement P0, establish objective benchmark harness, then build P1 near-field voxel renderer before investing in distant-world or advanced lighting optimization.
