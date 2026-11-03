# ADR 0001: A measurable test bed before a full game

Status: implemented experiment, 2026-11-03.

The original repository had no commits or source branch. The user authorized creating a test bed using the attached architecture. The proposal is a design target; it is not evidence that those systems or performance levels already exist.

## Choices

- C++20 and CMake. Keep world/meshing/storage/tasks independent of Vulkan and GLFW. Singleplayer-first laboratory; networking decisions remain open.
- GLFW for the first window/input prototype; Vulkan 1.3 dynamic rendering and synchronization2 are required and checked. Missing validation layers, required features, attachment formats, or timestamps cause explicit errors. No mesh shader, bindless, ray-tracing, or sparse-resource dependency.
- HLSL with an optional DXC path and tested glslang fallback. Current shaders use vertex colors and distance fog. Their simplicity isolates terrain correctness and basic submission cost.
- Fixed render pass schedule: depth-tested opaque terrain → transfer/readback or blit → presentation. Explicit barriers enforce resource dependencies. This is not a resource-aliasing/general frame graph. One in-flight render frame makes early lifetime reasoning tractable; timestamps and real frame measurements will motivate later pipelining.
- Main-thread viewer startup/edit rebuilds are deliberately simple and measured. A separate streaming laboratory tests bounded jobs, epoch cancellation, upload admission, eviction and edit invalidation. Connecting those systems without introducing frame stalls is the next integration gate.
- Shared priority queue with stable FIFO ties and bounded admission, rather than premature work stealing. Smaller priority numbers win. Running work checks cancellation between generation/meshing stages; a stage is not preempted. Destruction joins outstanding work.
- Cache key is chunk origin; changing chunk side invalidates the cache. World revision changes invalidate existing meshes. Pending requests are deduplicated within each epoch. Only the owner thread calls streamer APIs; world reads/edits are synchronized. Metadata entry count and pending work are independently bounded to prevent empty chunks escaping byte caps.
- Terrain generation version 1 uses stable unsigned hashing and smooth interpolated lattice heights. It is an initial deterministic source, not a full biome/erosion pipeline. CPU world positions use int64; camera uses doubles. The current renderer is an origin-centered fixed patch, so long-range camera-relative streaming remains unimplemented.
- Edit format `VCED`: magic, little-endian schema/generation version 1, uint64 seed/count, sorted int64 xyz + material entries, trailing FNV-1a-64 corruption checksum. At most 64 MiB per snapshot; material IDs 0..3. Loading validates the entire snapshot before replacing current edits. One writer per world file. Same-directory temporary write, OS data flush, atomic replace, and POSIX directory flush protect the previous snapshot; filesystem/hardware guarantees still apply. A crash may leave an uncommitted `.tmp`; it is not automatically trusted or loaded. There is no journal, region compression, migration or automatic recovery yet.
- Near meshes use exact block queries; experimental distant surface rings sample the shared height function only. Rings are spatially bounded and have skirts, not full LOD correctness. High-impact edit summaries and volumetric mid terrain are prerequisites for claiming the proposed three-tier design.

## Next exit gates

1. Integrate chunk results into the viewer with prioritized requests, per-frame uploads, camera-relative coordinates and nonblocking edit remeshing. Test flight, teleports and disk loads while rendering.
2. Implement volumetric mid LOD and edit-aware far summaries; compare shared boundaries, geometry error, silhouette changes and temporal flicker. Skirts alone cannot pass this gate.
3. Add a general frame graph, Tracy scopes, persistent pipeline caches, GPU culling/indirect draws, GPU memory telemetry and pass timings before expensive lighting.
4. Define a visual reference preset. The user confirmed the 4K PC has Windows 11, RTX 3080 10 GB and 32 GB DDR5, and the 1440p PC has RX 9060 XT 16 GB and 64 GB RAM. CPU/driver details remain to be recorded per machine. Capture GPU/CPU traces on the actual Ryzen 7800X3D/RTX 3080 at the stated resolutions. Only then evaluate 100 FPS; 200 FPS remains a stretch goal.
5. Add game systems after those measurements establish a viable playable vertical slice.

The test bed does not claim completion of P0: Tracy and a general frame graph are still missing. It establishes a runnable correctness/performance laboratory so those decisions can be tested rather than assumed.
