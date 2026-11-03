# Test-bed validation and measurement notes

## Verified in this cloud instance

2026-11-03. GCC 14.2, CMake 3.31.6, glslang 15.1, Vulkan loader 1.4.309, Mesa llvmpipe 25.0.7 (CPU/software rendering). Builds use C++20, Release, four worker threads. These are diagnostic cloud measurements, not controlled gaming-PC benchmarks. Other cloud activity can affect timings.

- 12 core tests passed: negative/large coordinates, generation at 16³/32³, greedy face reduction, neighbor culling, brute-force mesh coverage, save overwrite/roundtrip/corruption/transactionality, ray edits/collision, bounded far-ring sampling, scheduler priority/admission, cancellation/cache caps, dedup/edit invalidation/chunk-size changes, and empty-cache metadata/upload limits.
- CTest passed all three Release targets: core, CPU streaming smoke, and offscreen Vulkan validation/readback.
- ASan + UBSan + leak detection passed both CPU CTest targets. LeakSanitizer cannot run under the command sandbox's ptrace; the successful runs used supported command escalation without disabling checks.
- Eight 600-frame CPU workloads passed: stationary/flight/teleport/edits × 16³/32³, with a 1 MiB mesh cache, at most 32 pending jobs and a 2 MiB upload-admission quota. The strengthened checks require nonempty current geometry after settling, not just cancelled/completed futures.
- Sixteen 60-frame Vulkan workloads passed: stationary/flight/yaw/edits × 640×360/1080p/1440p/4K. All had zero validation errors, nonzero GPU work and actual sky/terrain pixels in readback. Five startup samples are omitted from renderer summaries.
- Windowed GLFW/swapchain smoke passed 24 frames at 640×360 under Xvfb with synchronization validation. An acquisition-to-layout-transition hazard found during development was fixed and the check rerun cleanly.
- Saved startup command passed 120 offscreen flight frames with zero validation errors. SDK installation refresh and retained-dependency setup paths both passed.

Windows builds, CI-host executions, actual NVIDIA/AMD performance, RenderDoc captures and fresh-task restoration have not been verified. No 100/200 FPS or final visual-quality claim is supported by this run.

## CPU observations

Times below are milliseconds. Main-thread timings cover request/poll work, excluding the artificial 2 ms sleep. Generation+meshing is a separate serial sample over 32 chunks, not end-to-end asynchronous queue latency. Settle time starts when the movement/edit workload ends, drains stale work and requests current terrain. It includes already-resident cache hits for stable scenarios. Percentiles select the sorted sample at `floor((n-1)*p)`.

| Workload | Chunk | Main p99 ms | Generation+mesh p99 ms | Post-workload settle ms |
|---|---|---:|---:|---:|
| stationary | 16³ | 0.006 | 1.200 | 0.002 |
| flight | 16³ | 0.084 | 0.920 | 1.090 |
| teleport | 16³ | 0.161 | 1.995 | 0.001 |
| edits | 16³ | 0.476 | 2.753 | 6.850 |
| stationary | 32³ | 0.007 | 8.113 | 0.001 |
| flight | 32³ | 0.165 | 6.867 | 0.002 |
| teleport | 32³ | 0.257 | 7.814 | 0.001 |
| edits | 32³ | 4.876 | 7.326 | 33.601 |

The flight cases reached the cache cap and evicted meshes without exceeding it. Under a sustained edit every 2 ms, 32³ generation repeatedly lost its epoch: no geometry became resident during that high-churn phase in this run. Once edits stopped, current terrain became resident. This exposes the cost of conservative whole-world revision invalidation; dirty-region invalidation and retaining the previous valid mesh are needed before integrating this streamer into the viewer. A passing bound/recovery test does not establish stall-free gameplay.

## Render observations

Flight scenario, flat vertex colors + depth + fog. GPU timestamps measure the opaque pass. Frame-wall timings include fence waits and submission; CPU/GPU timings overlap and must not be added. CSV GPU samples describe the previous submitted frame. Screenshots are copied once after the measured loop. Windowed mode uses FIFO/vsync; offscreen mode has no presentation. Initial scene generation is excluded; subsequent synchronous edit rebuilds are included and can cause large stalls.

| Output | Frame-wall p99 ms | GPU opaque median ms | Validation errors |
|---|---:|---:|---:|
| 640×360 | 14.24 | 8.50 | 0 |
| 1920×1080 | 30.01 | 18.64 | 0 |
| 2560×1440 | 26.55 | 21.14 | 0 |
| 3840×2160 | 63.93 | 44.46 | 0 |

Maximum process peak RSS among these workload processes was approximately 199.0 MiB. Cache counters cover retained mesh vector capacities; allocator metadata is separately count-bounded. Transient jobs, renderer allocations and driver memory are not part of that cache cap. There is no measured VRAM high-water counter yet. The one-percent-low value is 1000 divided by the mean of the slowest ceil(1%) frame samples; it is not the reciprocal of the p99 value.

## Reproduction and evidence

Raw summary and compiled-workload source hashes: [cloud-baseline.json](cloud-baseline.json). Full CSVs, logs and PPMs remain in the ignored artifact directories named there. The CPU rows come from the strengthened recovery-check rerun; the renderer rows come from the earlier full matrix, whose renderer source was unchanged. These results are one run, not cross-hardware rankings.

```bash
source tools/cloud-env.sh
bash tools/setup-cloud.sh
python3 tools/stress.py --frames 600 --full
```

For target-PC runs, use the recorded hardware profiles in `config/hardware_profiles.json` on the actual PC, with 600 or more rendered frames per scenario. The user confirmed Windows 11 / RTX 3080 10 GB / 32 GB DDR5 for 4K and RX 9060 XT 16 GB / 64 GB RAM for 1440p. The second PC's OS and both systems' exact driver/CPU configuration still need to be recorded with measurements. Profiles annotate a requested target; the real Vulkan device is always recorded separately.
