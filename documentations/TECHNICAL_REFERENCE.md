# Frost Particle Meshing - Technical Reference

## Scope

This document describes the current architecture of the Blender addon and the native CPU / CUDA backends as of addon version `1.23.0`.

Supported Blender version: `5+`

---

## Architecture

High-level stack:

```text
Blender UI / bpy
  -> frost_blender_addon (Python)
  -> blender_frost_adapter.pyd (Pybind11 / C++)
  -> CPU Thinkbox Frost core
  -> CUDA FrostGPUManager
```

Main Python files:

- `frost_blender_addon/operator.py`
- `frost_blender_addon/ui.py`
- `frost_blender_addon/particle_extractor.py`

Main native files:

- `blender_frost_adapter/src/adapter.cpp`
- `blender_frost_adapter/src/frost_interface.cpp`
- `blender_frost_adapter/src/cuda/frost_cuda.cu`

---

## Python Layer

### Responsibilities

- collect source particles from Blender
- transform source positions into the Frost object's local space
- map Blender UI properties to native parameters
- write the resulting mesh back to Blender with `foreach_set`

### Current Mesh Source Limitation

`extract_from_mesh_vertices()` currently reads `obj.data` directly for `MESH` sources.

Implication:

- non-applied mesh modifiers are not evaluated automatically as Frost source geometry

This is currently a documented limitation rather than a hidden bug.

---

## Native Interface

`FrostInterface` is the unified entry point exposed through Pybind11.

Main methods:

- `set_particles(positions, radii, velocities)`
- `set_parameter(name, value)`
- `set_parameters(dict)`
- `generate_mesh()`

The CPU path dispatches to the upstream Thinkbox Frost meshing pipeline.

The GPU path builds its own scalar field and extracts a mesh before entering the shared post-processing stage.

---

## Current GPU Pipeline

The current CUDA path no longer uses the original simple blob-density approach as the final surface model.

### 1. Particle Upload

The GPU now receives:

- particle positions
- particle radii

This is required for a Zhu-Bridson-style field because the field depends on a blended radius term, not just density.

### 2. Neighbor Search

`cuNSearch` is used to maintain the GPU-oriented neighborhood stage and keep the overall pipeline structured around local particle influence.

### 3. Zhu-Bridson Grid Accumulation

The grid accumulation stage stores:

- total weight
- blended radii accumulator
- blended offset accumulator

Conceptually, each voxel gathers weighted particle influence inside the search radius.

### 4. Field Finalization

Each voxel is converted into the scalar field using:

- blended offset magnitude
- blended radius term
- optional low-density compensation term

The GPU extraction now uses an isovalue of `0.0`.

### 5. Marching Cubes

Marching Cubes runs on the finalized field and emits raw triangle vertices.

Important recent stability changes:

- more stable edge interpolation for nearly equal field values
- no premature tiny-triangle culling inside the CUDA kernel

This matters because deleting triangles too early can create open seams between neighboring cells.

### 6. CPU-Side Weld and Post-Process

After download, the C++ layer:

- welds vertices on a tolerance grid
- rejects only triangles that collapse to duplicate welded vertex indices
- applies shared post-processing such as push / relax

The previous near-zero-area rejection after weld was removed because it could also remove triangles that were still topologically necessary.

---

## GPU UI Mapping

Current exposed GPU controls:

- `meshing_resolution_mode`
- `meshing_resolution`
- `meshing_voxel_length`
- `gpu_search_radius_scale`

Current hidden / internal GPU controls:

- `gpu_block_size`
- `gpu_surface_refinement`
- GPU low-density trimming values

### Why Block Size Is Hidden

`gpu_block_size` only affects the 1D CUDA launch size used for field accumulation / finalization.

It does not meaningfully change the meshing result itself.

Also note:

- the Marching Cubes kernel uses a fixed `8 x 8 x 8` block configuration
- the normal workflow now hardcodes the internal block size to `256`

---

## CPU Path

The CPU path still relies on the upstream Thinkbox Frost algorithms, including:

- Union of Spheres
- Metaball
- Zhu-Bridson
- Anisotropic

This remains the feature-complete path and the visual quality reference for future GPU work.

---

## Build Notes

Typical Windows native build:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build K:\Codex_Projects\Projet_Frost_Plugin_For_Blender_CODEX\build_codex --config Release --parallel 8'
```

After a successful build, copy the generated `.pyd` into `frost_blender_addon/`.

Blender must be restarted to load the updated native module.

---

## Validation Notes

Recent regression checks used:

- headless Blender tests
- Suzanne source meshes
- subdivided Suzanne source meshes
- topology checks for boundary edges and degenerate faces

The current CUDA seam fix was validated by reducing boundary-edge counts to `0` on these regression meshes.

---

## Known Limitations

- GPU path is still less feature-complete than the Thinkbox CPU path.
- `MESH` sources do not yet use evaluated modifier-stack geometry automatically.
- GPU mode is CUDA-only.

---

## Next Good Targets

- evaluated mesh extraction for `MESH` sources
- further visual convergence between CUDA Zhu-Bridson and Thinkbox CPU Zhu-Bridson
- stronger automated regression tests around topology and shading artifacts

---

Document version: `1.2`
Last update: `2026-03-22`
