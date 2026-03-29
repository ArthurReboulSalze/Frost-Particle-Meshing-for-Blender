# Frost for Blender - Documentation Index

Current addon version: `1.26.0`  
Last update: `2026-03-29`

## Main Documents

### User Guide

[USER_GUIDE.md](USER_GUIDE.md)

For day-to-day usage:

- installation
- panel overview
- CPU and GPU workflow
- animation
- troubleshooting

### Technical Reference

[TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md)

For development and maintenance:

- addon architecture
- native bridge
- CPU / CUDA / Vulkan backend structure
- current limits
- build notes

### Changelog

[CHANGELOG.md](CHANGELOG.md)

Release history and notable technical changes.

## Quick Notes

- The public package is now a single Blender zip for the current `CPU + Vulkan` workflow.
- The Vulkan backend is still in active development.
- Current testing indicates that CPU still usually wins on low-poly scenes, while heavier / high-poly scenes can now bring Vulkan close to parity or ahead depending on the case.
- `Vertex Refinement` still forces the final surface build back to the CPU path.
- `MESH` and `POINT_CLOUD` sources now read evaluated Blender geometry, including deformations and caches active in the current frame.
