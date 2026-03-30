# Frost for Blender Addon Package

Current addon version: `1.26.1`  
Supported Blender version: `5+`

## Included

- Thinkbox Frost CPU meshing path
- Current Vulkan GPU backend work
- Blender Python addon files
- `frost_native.dll` and bundled runtime dependencies
- extension manifest for Blender Extensions work
- packaged license / notice files for future extension distribution

## Notes

- The current public package is a single `CPU + Vulkan` addon package.
- The Vulkan backend is still in active development.
- Current testing indicates that CPU still usually wins on low-poly scenes, while heavier / high-poly scenes can now bring Vulkan close to parity or ahead depending on the case.
- `Vertex Refinement` still forces the final surface build back to CPU.
- Evaluated Blender geometry is now used for animated `MESH` and `POINT_CLOUD` sources.
- Restart Blender after replacing the addon files or updating `frost_native.dll`.
- For Blender Extensions work, review `LICENSE.txt` and `THIRD_PARTY_NOTICES.md` in this folder.

## Credits

- Arthur Reboul Salze
- AWS Thinkbox Frost core
- Codex
