# Frost for Blender - Changelog

Current addon version: `1.26.1`  
Last updated: `2026-03-30`

---

## v1.26.1 - Blender Extensions Packaging Baseline (2026-03-30)

### Added / Improved

- Added a first valid `blender_manifest.toml` for Blender Extensions packaging in the addon folder.
- Added packaged extension notice files:
  - `LICENSE.txt`
  - `THIRD_PARTY_NOTICES.md`
- Removed the manual `sys.path` mutation from the addon package to align better with Blender Extensions expectations.
- Added a dedicated compliance tracking note for `extensions.blender.org`.
- Validated that Blender `5.1` can now parse the manifest and build a local extension archive from the addon folder.

### Fixed

- Excluded legacy adapter artifacts from the extension build, including old `.pyd` files and the bundled CUDA runtime that is not part of the current public `CPU + Vulkan` package.
- Tightened extension build exclusions so temporary validation helpers are not bundled in the generated extension zip.

### Notes

- This release improves packaging and submission readiness, not the Frost meshing feature set itself.
- The current public runtime remains the same `CPU + Vulkan` addon workflow.
- Submission to `extensions.blender.org` still requires a final review of the native binary strategy.

---

## v1.26.0 - Evaluated Sources and Vulkan Fallback Reuse (2026-03-29)

### Added / Improved

- `MESH` and `POINT_CLOUD` sources now read Blender evaluated geometry, so Frost follows shape keys, armatures, animated modifiers, Alembic caches, and animated point data.
- Reworked the Vulkan raw-to-safe fallback so the backend can reuse the already computed scalar field and classified surface cells instead of recomputing the whole frame.
- Improved the direct Vulkan release candidate path on dense `Metaball` scenes by fixing resident surface compaction and by tightening the adaptive pack selection.
- Refreshed the public documentation for the current `CPU + Vulkan` workflow and the latest measured performance trends.

### Fixed

- Fixed disappearing / collapsed GPU meshes on dense `Metaball` frames where vertices could end up compacted to invalid positions.
- Fixed several Vulkan heavy-frame regressions where the pack path became pathologically slow on medium and dense resolutions.
- Fixed animated source extraction so deformed Blender geometry is actually reflected in the generated Frost mesh.
- Reduced duplicate Vulkan work when the raw GPU path is rejected and the backend has to continue through the safer GPU surface extraction path.

### Notes

- CPU remains the stable reference path.
- Vulkan is still under active optimization, but the gap has tightened significantly on heavier scenes.
- `Vertex Refinement` still forces the final surface build back to the CPU path for now.

---

## v1.25.0 - Vulkan Stability, UI Reporting, and Unified Release (2026-03-27)

### Added / Improved

- Continued the Vulkan meshing pipeline so field evaluation, surface-cell classification, triangle emission, and compact GPU-oriented surface assembly can run in the current native backend.
- Added UI reporting for the last meshing backend used, including explicit CPU fallback reporting when GPU was requested but Frost had to switch back for safety.
- Improved the native bridge and addon reporting so Blender can show whether the last run used:
  - `Vulkan`
  - `Vulkan Raw`
  - `CPU`
  - `CPU Fallback`
- Simplified the root README and refreshed the maintained documentation around the current single-package `CPU + Vulkan` release model.
- Prepared the project for the new GitHub repository name: `Frost-for-Blender`.

### Fixed

- Added stricter validation of raw GPU mesh buffers before they are handed to Blender.
- Added stronger safety fallback behavior when the direct Vulkan surface path produces invalid or unstable geometry.
- Reduced the risk of Blender closing on heavier GPU scenes by preferring safer fallback paths instead of letting invalid mesh data reach Blender.
- Fixed status reporting so the addon now reflects the backend actually used for the last meshing run.

### Notes

- The Vulkan backend is still in active development.
- Current testing indicates that Vulkan is usually slower than the CPU path on low-poly scenes, while heavier / high-poly scenes can already favor Vulkan depending on the case.
- `Vertex Refinement` still forces the final surface build back to the CPU path for now.

---

## v1.24.1 - GPU Mesh Stability and Unified Release Package (2026-03-26)

### Added / Improved

- Prepared a single Blender-installable addon package for the `CPU + Vulkan` workflow.
- Updated repository references for the simplified GitHub project name direction.
- Reduced unnecessary host work on the Vulkan direct path by skipping output particle readback when it is not needed.

### Fixed

- Disabled direct GPU surface extraction whenever `Vertex Refinement` is enabled, so those cases intentionally fall back to the stable CPU meshing path.
- Fixed dense Vulkan direct-surface assembly issues where invalid leftover faces could create holes, spikes, or stretched triangles.
- Revalidated the rebuilt native path under Blender `5.1`.

---

## v1.24.0 - Blender 5.1 Support and Experimental Vulkan Backend (2026-03-26)

### Added / Improved

- Added a stable `ctypes -> frost_native.dll` bridge so the addon is no longer tied to a single Blender Python ABI.
- Verified addon loading and native meshing under Blender `5.1`.
- Introduced a GPU backend abstraction layer and an initial Vulkan backend.
- Added Vulkan runtime, compute, and storage-buffer probing in the native layer.
- Added the first usable Vulkan field / coverage / surface-cell / triangle-emission workflow.

### Notes

- CPU remained the reference path for feature completeness and visual stability.
- `MESH` source extraction still sampled `obj.data` directly instead of evaluated modifier-stack geometry.

---

## v1.23.0 - CUDA Zhu-Bridson Stability and UI Cleanup (2026-03-22)

### Added / Improved

- Reworked the GPU scalar field toward a more faithful Zhu-Bridson-style field instead of a simple blob density field.
- Uploaded particle radii to the GPU and used blended radius / blended offset terms during field evaluation.
- Improved Marching Cubes edge interpolation stability for nearly equal scalar values.

### Fixed

- Fixed remaining open seams / unwelded-looking cracks in the CUDA mesh path by removing premature triangle culling in the kernel.
- Fixed additional topology loss in the post-weld stage by only rejecting triangles that collapse to duplicate indices.

---

## Historical Milestones

### v22.0 - Debug Log Toggle and Alembic Export Fix (2026-03-03)

- Added `Show Debug Log`.
- Reworked Bake to Alembic to export frame by frame.

### v20.6 - GPU Grid Alignment and Stability (2026-01-07)

- Fixed GPU mesh shifts caused by grid alignment changes.

### v20.4 - Marching Cubes Table Correction (2026-01-06)

- Replaced corrupted Marching Cubes lookup tables with verified reference tables.

### v19.x - Initial GPU Marching Cubes Bring-Up

- First usable CUDA meshing path.

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

## Credits

- Arthur Reboul Salze
- AWS Thinkbox Frost core
- Codex
