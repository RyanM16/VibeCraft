# VibeCraft Beta V2.1

This patch advances the spec’s `VC-WORLD-1` terrain goal. The playable sandbox now renders a distant horizon from the same seeded height function used by exact voxel terrain. Build and run instructions are in the [README](https://github.com/RyanM16/VibeCraft/blob/beta-v2.1/README.md#launch).

- The default horizon extends 128 chunks along each horizontal axis. Choose `--horizon 512` for the 8,192-block research profile or `--horizon local` for the local-only view.
- Surface bands have matching edges along resolution changes. The rolling terrain worker builds and publishes the combined near and far geometry in camera-relative coordinates.
- Collision, block edits, and save compatibility use the exact voxel world. The horizon controls the view distance independently of the editable radius.

## Validation

Release CTest and ASan/UBSan CTest passed. Release tests included the automated edge test for the 512-chunk mesh at a negative world origin and the offscreen Vulkan smoke test. The edge test verifies matching positions and connectivity throughout the distant surface bands. Windowed gameplay ran on Mesa llvmpipe under Linux at 640×360, with zero Vulkan validation errors. The game reopened an existing Beta V2 save and targeted the previously placed block. During a 512-chunk play test, the active mesh center moved from (0, 0) to (16, 0) and the terrain queue drained.

| Profile | Distant triangles | Combined CPU mesh capacity |
|---|---:|---:|
| Local | 0 | 748 KiB |
| 128 chunks | 33,344 | 2,385 KiB |
| 512 chunks | 46,400 | 3,485 KiB |

Counts are from seed 42 at spawn with the default editable radius. Capacity measures CPU mesh vectors and excludes staging and GPU resources.

## Remaining work

The seam between the exact voxel window and the first surface band uses a skirt. The distant mesh uses generated heights, so distant edits, caves, and overhangs wait for later representations. The surface is rebuilt about a new center as you travel, and transitions can pop. The patch leaves the generator and save schema compatible with Beta V2. Native Windows Vulkan, dedicated GPU performance, and the complete `VC-WORLD-1` gate remain open.
