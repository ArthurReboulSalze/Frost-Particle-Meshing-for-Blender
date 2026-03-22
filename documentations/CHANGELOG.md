# Frost Particle Meshing - Changelog

## Current Release

Current addon version: `1.23.0`
Last updated: `2026-03-22`

Older entries below keep some of the original internal milestone numbering from development logs, but the addon version shown in Blender now follows the `1.x.y` release line.

---

## v1.23.0 - CUDA Zhu-Bridson Stability and UI Cleanup (2026-03-22)

### Added / Improved

- Reworked the GPU scalar field so it behaves like a real Zhu-Bridson-style field instead of a simple blob density field.
- Uploads particle radii to the GPU and uses blended radius / blended offset terms during field evaluation.
- Uses an isovalue of `0.0` for the GPU Zhu-Bridson field.
- Improved Marching Cubes edge interpolation stability for near-equal scalar values.

### Fixed

- Fixed remaining open seams / unwelded-looking cracks in the CUDA mesh path by removing premature triangle culling in the CUDA kernel.
- Fixed additional topology loss in the post-weld stage by only rejecting triangles that collapse to duplicate indices.
- Verified closed topology on Suzanne-based regression tests, including subdivided source geometry.

### UI / Workflow

- Removed `Block Size` from the normal GPU UI.
- Disabled `Low Density Trimming` in the current exposed workflow.
- Disabled `Surface Refinement` in the current exposed workflow.
- Kept the internal GPU block size at `256` as an implementation detail.

### Notes

- `MESH` source extraction still samples `obj.data` directly, so non-applied mesh modifiers are not yet used automatically as Frost source geometry.

---

## Historical Milestones

### v22.0 - Debug Log Toggle and Alembic Export Fix (2026-03-03)

- Added `Show Debug Log` toggle.
- Reworked Bake to Alembic to export frame by frame.
- Fixed build script handling for version-suffixed `.pyd` outputs.

### v20.6 - GPU Grid Alignment and Stability (2026-01-07)

- Fixed GPU mesh shifts caused by grid alignment changes.
- Cleaned up CPU / GPU separation in the panel.

### v20.4 - Marching Cubes Table Correction (2026-01-06)

- Replaced corrupted Marching Cubes lookup tables with verified reference tables.
- Resolved major holes and malformed triangle output caused by table corruption.

### v19.x - Initial GPU Marching Cubes Bring-Up

- First usable CUDA meshing path.
- Established FrostGPUManager and native GPU bridge flow.

### v17.x - CPU TBB and Data Pipeline Optimization

- Restored broad TBB parallelism.
- Reduced Python-side mesh update overhead with `foreach_set`.

### v15.x - Alembic Export

- Added Bake to Alembic workflow.

### v13.x - Multi-Source Support

- Added multiple additional Frost source objects.

### v12.x - Additional CPU Methods

- Added Metaball and Anisotropic CPU modes.

### v11.x - Zhu-Bridson CPU

- Exposed the Thinkbox Zhu-Bridson path through the Blender addon.

### v10.x - First Functional Meshing in Blender

- First working Frost mesh generation from Blender-side particle data.

---

## Next Likely Improvements

- Support evaluated mesh extraction for `MESH` sources so Subdivision Surface modifiers can be used without applying them.
- Continue reducing the quality gap between CUDA Zhu-Bridson and the CPU Thinkbox path.
- Add better GPU-focused regression tests around topology and surface quality.

---

## Credits

- Arthur Reboul Salze
- AWS Thinkbox Frost core
- Codex
- ChatGPT 5.4
