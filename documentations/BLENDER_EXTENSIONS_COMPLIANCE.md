# Blender Extensions Compliance Notes

Last update: `2026-03-30`

This document keeps track of the current analysis for a future submission of Frost for Blender to `extensions.blender.org`.

It is a practical compliance note for the project, not legal advice.

## Current Conclusion

The project looks *likely* publishable on `extensions.blender.org`, but not in its current packaging state.

The main legal point looks acceptable:

- the vendored Thinkbox Frost core is distributed under Apache License 2.0 in [thinkbox-frost/LICENSE.txt](../thinkbox-frost/LICENSE.txt)
- Blender Extensions requires add-ons to be published under `GPL-3.0-or-later`
- Apache 2.0 is compatible with GPLv3

So the likely path is:

- publish the extension itself under `GPL-3.0-or-later`
- keep the original Apache 2.0 notices for the Frost core
- keep the third-party attribution files already shipped with Frost

## Licensing Notes

### Frost Core Provenance

Relevant local files:

- [thinkbox-frost/LICENSE.txt](../thinkbox-frost/LICENSE.txt)
- [thinkbox-frost/NOTICE.txt](../thinkbox-frost/NOTICE.txt)
- [thinkbox-frost/THIRD-PARTY-LICENSES](../thinkbox-frost/THIRD-PARTY-LICENSES)

Current observation:

- the Frost source bundled in this repository is under Apache License 2.0
- Apache 2.0 allows redistribution and modification
- this supports using the Frost code as the base of the addon, including for a free public release

### Blender Extensions License Requirement

Official Blender documentation states:

- Blender Extensions only supports free and open-source extensions
- for add-ons, the required license is `GNU General Public License v3.0 or later`

Practical consequence for this project:

- the extension package submitted to Blender should declare `GPL-3.0-or-later`
- Apache 2.0 notices from Frost must still be preserved in the shipped source tree

## Current Compliance Risks

### 1. Initial Extension Manifest Added, But Packaging Is Not Finished

An initial `blender_manifest.toml` now exists in:

- [frost_blender_addon/blender_manifest.toml](../frost_blender_addon/blender_manifest.toml)

This is an important first step, but it does **not** mean the project is fully ready for submission yet.

Open points still remain:

- final review of the manifest fields
- package-local license / notice layout for the shipped extension zip
- final validation of the native dependency strategy
- final extension build and validation pass

### 2. Extension Package License / Notice Files Need Final Review

The extension package now includes:

- [frost_blender_addon/LICENSE.txt](../frost_blender_addon/LICENSE.txt)
- [frost_blender_addon/THIRD_PARTY_NOTICES.md](../frost_blender_addon/THIRD_PARTY_NOTICES.md)

This is a packaging improvement, but a final release pass is still needed to confirm:

- exact shipped license text layout
- exact attribution wording
- whether additional bundled runtime libraries need their own copied notices inside the extension package

### 3. Native Binary Packaging Is the Biggest Review Risk

Relevant local files:

- [frost_blender_addon/native_bridge.py](../frost_blender_addon/native_bridge.py)
- [frost_blender_addon/operator.py](../frost_blender_addon/operator.py)

Current behavior:

- the addon loads `frost_native.dll` with `ctypes.CDLL`
- it uses `os.add_dll_directory(...)`
- it ships multiple native DLL dependencies directly in the addon folder

Important note:

- Blender officially documents `Python Wheels` as the recommended way to bundle dependencies for extensions
- no explicit official sentence was found here saying that `ctypes`-loaded DLLs are absolutely forbidden
- however, this should be treated as a likely review risk until proven otherwise

This point is an inference from the official packaging direction, not a confirmed rejection rule.

### 4. Import / Namespace Conformance Needs Cleanup

Relevant local file:

- [frost_blender_addon/__init__.py](../frost_blender_addon/__init__.py)

Current behavior before the first cleanup pass:

- the addon mutated `sys.path`

Blender extension documentation expects extension packages to work within their namespace and use relative imports.

Recommendation:

- remove the manual `sys.path` insertion
- convert all internal imports to clean relative imports only

Status:

- the manual `sys.path` insertion has now been removed from [frost_blender_addon/__init__.py](../frost_blender_addon/__init__.py)
- internal addon imports already use relative imports

### 5. Package Cleanliness

The repository contains development-only material that must not be part of an extension submission, for example:

- `benchmark/`
- `build/`
- `build_native/`
- `build_vulkan/`
- `dist_packages/`
- temporary scripts such as `tmp_*.py`

The final extension package should exclude:

- `.blend` benchmark files
- build folders
- logs
- temporary scripts
- obsolete binaries not required by the shipped extension

Status:

- `blender_manifest.toml` now excludes legacy `.pyd` adapter files from the extension build
- `cudart64_12.dll` is also excluded from the extension build, because the current public path targets `CPU + Vulkan`, not CUDA

### 6. Permissions Need To Be Declared Correctly

The addon performs file operations, for example Alembic export paths and folder creation in:

- [frost_blender_addon/operator.py](../frost_blender_addon/operator.py)

For Blender Extensions, the manifest should declare only the permissions actually needed.

The likely permission needed here is:

- `files`

## Current Submission Readiness

### Likely Acceptable

- free public release of the project
- publishing source derived from the Apache-2.0 Frost core
- shipping the add-on under `GPL-3.0-or-later`, while preserving Frost notices

### Not Ready Yet

- no explicit root license file for the extension package
- legacy import/path behavior still present
- native binary packaging not yet aligned with the documented extension dependency path
- release package still needs stricter exclusion rules

## Recommended Plan

1. Add a root `LICENSE` file for the extension itself using `GPL-3.0-or-later`.
2. Keep `thinkbox-frost/LICENSE.txt`, `NOTICE.txt`, and `THIRD-PARTY-LICENSES` in the repository and release source.
3. Create `frost_blender_addon/blender_manifest.toml`.
4. Remove `sys.path` mutation and keep only relative imports.
5. Audit the addon package so only required runtime files are shipped.
6. Add manifest permissions only where necessary, likely `files`.
7. Evaluate whether the native runtime should be repackaged as Blender extension wheels instead of raw DLL loading.
8. Validate locally with Blender extension CLI before any submission.

Current progress:

- step 3 started
- step 4 started
- step 5 started

## Validation Status

Blender 5.1 extension tooling now accepts the manifest and can build an extension archive from the addon folder.

Latest local checks:

- `blender --command extension validate` on `frost_blender_addon/`: passed
- `blender --command extension build` on `frost_blender_addon/`: passed

Current test archive produced locally:

- [dist_packages/extension_test/frost_particle_meshing-1.26.1.zip](../dist_packages/extension_test/frost_particle_meshing-1.26.1.zip)

Important note:

- passing `validate` and `build` does **not** guarantee acceptance on `extensions.blender.org`
- it only confirms that the package structure and manifest are now valid enough for Blender's local extension tooling

## Practical Recommendation

Before attempting submission to `extensions.blender.org`, the project should be treated as:

- legally plausible
- technically non-compliant yet
- likely to require packaging refactor more than core code relicensing

The most important open question is no longer the Frost/AWS provenance.

The most important open question is:

- whether the current native `ctypes + DLL` packaging model will pass Blender Extensions review, or should be converted to a wheel-based dependency layout

## Official References

- [Blender Extensions submission page](https://extensions.blender.org/submit/)
- [Blender Manual: Creating Extensions](https://docs.blender.org/manual/en/dev/advanced/extensions/)
- [Blender Manual: Extension Licenses](https://docs.blender.org/manual/en/dev/advanced/extensions/licenses.html)
- [Blender Manual: Add-ons](https://docs.blender.org/manual/en/dev/advanced/extensions/addons.html)
- [Blender Manual: Python Wheels](https://docs.blender.org/manual/en/dev/advanced/extensions/python_wheels.html)
- [Blender Extensions Terms of Service](https://extensions.blender.org/terms-of-service/)
- [Blender Developer Docs: Add-on Guidelines](https://developer.blender.org/docs/handbook/extensions/addon_guidelines/)
- [Blender Developer Docs: Extension Schema 1.0.0](https://developer.blender.org/docs/features/extensions/schema/1.0.0/)
- [Blender Manual: Extension CLI Arguments](https://docs.blender.org/manual/en/latest/advanced/command_line/extension_arguments.html)
- [GNU license list](https://www.gnu.org/licenses/license-list.en.html)
