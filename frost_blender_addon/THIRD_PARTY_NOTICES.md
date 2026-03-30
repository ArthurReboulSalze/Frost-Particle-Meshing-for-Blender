# Third-Party Notices

This addon package includes or derives from third-party components.

## Thinkbox Frost Core

The Frost core integrated in this project comes from the Thinkbox Frost source release.

Upstream local files:

- [../thinkbox-frost/LICENSE.txt](../thinkbox-frost/LICENSE.txt)
- [../thinkbox-frost/NOTICE.txt](../thinkbox-frost/NOTICE.txt)
- [../thinkbox-frost/THIRD-PARTY-LICENSES](../thinkbox-frost/THIRD-PARTY-LICENSES)

Relevant notice:

> Frost  
> Copyright 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.

License status used by this project:

- upstream Frost source is distributed under Apache License 2.0
- this addon package is intended to be distributed as a GPL-3.0-or-later Blender extension
- the upstream Frost notices and attribution files must be preserved

## Runtime Libraries

The addon package currently includes bundled native runtime files used by the Frost native bridge on Windows.

Examples currently present in the addon package:

- `frost_native.dll`
- `tbb12.dll`
- `boost_*.dll`
- `OpenEXR*.dll`
- related runtime dependencies required by the native Frost bridge

Before any `extensions.blender.org` submission, these shipped binaries should be reviewed again to confirm:

- which files are actually required at runtime
- which files must be preserved for license compliance
- whether Blender Extensions review expects a wheel-based dependency layout instead of raw DLL loading

## Project Distribution Note

This file is a project tracking note for packaging and compliance work.

It does not replace the original upstream license files.
