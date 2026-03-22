# Frost Particle Meshing Addon

Blender addon package for Frost Particle Meshing.

Current addon version: `1.23.0`
Supported Blender version: `5+`

## Features

- CPU meshing via Thinkbox Frost
- CUDA Zhu-Bridson GPU meshing
- Particle systems, point clouds, and mesh vertex sources
- Auto Update workflow
- Bake to Alembic
- Push / smoothing post-processing

## Notes

- The normal GPU UI now exposes only the controls that materially affect the result.
- `Block Size`, `Low Density Trimming`, and `Surface Refinement` are not part of the normal GPU workflow anymore.
- For `MESH` sources, Frost currently reads raw mesh data, not evaluated modifier-stack geometry.

## Installation

1. Open Blender `5+`
2. Install the addon folder or symlink it into Blender's addon directory
3. Enable `Frost Particle Meshing`

## Native Module

The addon depends on `blender_frost_adapter.pyd` and the bundled runtime `.dll` files in the same directory.

If the native module is rebuilt, restart Blender so the updated binary is actually loaded.

## Package Variants

### Full CUDA package

This package includes:

- the Blender addon
- the native module with CUDA support
- the bundled CUDA runtime used by the plugin

This package does not include the full NVIDIA CUDA Toolkit.

To use GPU mode, the user still needs:

- an NVIDIA GPU
- a compatible NVIDIA driver installed on the machine

If those requirements are not met, the addon can still be installed, but GPU acceleration will not be usable.

### CPU-only package

This package includes:

- the Blender addon
- the native module compiled without CUDA support

Use this variant when:

- the machine has no NVIDIA GPU
- the user only wants CPU meshing
- you want the lightest and safest distribution

## Credits

- Arthur Reboul Salze
- AWS Thinkbox Frost core
- Codex
- ChatGPT 5.4
