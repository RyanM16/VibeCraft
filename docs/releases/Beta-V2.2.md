# VibeCraft Beta V2.2

This prerelease adds an opt-in cave generation profile. Start inside the entrance with `vibecraft --cave-start`, or begin at the surface with `vibecraft --caves`. The [README](https://github.com/RyanM16/VibeCraft/blob/beta-v2.2/README.md#explore-the-cave-prototype) has build and launch instructions.

- The seed determines each area's cave layout. A surface opening leads to a sloped tunnel, a broad hall, and two branches, one with a deeper descent. Smaller independent passages and chambers fill other parts of the area.
- Block generation, meshing, collision, and mining share the same cave field. The generator evaluates nearby cells on demand, and the existing voxel window streams underground as the player travels.
- New cave saves carry their generation profile in the snapshot version. Existing saves still use the original generator. Reopen a cave save with `--caves` or `--cave-start` and the same seed.

## Validation

Release CTest and ASan/UBSan CTest passed. An automated seed-42 flood fill reached both branches, more than 130 blocks east of the test entrance and more than 75 blocks below its start position. Tests also verified a smaller independent cave, matching terrain from 16³ and 32³ bricks, edit persistence, and rejection of incompatible save profiles.

A Linux windowed play test at 640×360 on Mesa llvmpipe walked about 26 blocks down the entrance, descended about 10 blocks, crossed a terrain window boundary, mined a block, and reopened the save. Both launches exited with zero Vulkan validation errors.

## Scope

The branching system is a prototype at roughly 180 blocks across, with a descent of roughly 90 blocks from the mouth to its lowest chamber. The spec's multi-kilometer megacaves, kilometer-deep shafts, and long underground views still need macro planning and cave-specific LOD. Ores, underground landmarks, depth ecology, and native Windows or dedicated GPU verification remain open.
