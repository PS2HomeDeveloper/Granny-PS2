# References and Project Boundaries

This repository is the game-side PS2 runtime. It consumes final `.mesh`, `.skeleton`, `.anim`, `.material`, and TIM2 resources. It does not parse FBX, link ufbx, or own source scene files.

Source-scene conversion belongs to the separate `ps2-asset-converter` repository. That repository owns the host compiler, ufbx dependency, source FBX or GLB readers, conversion reports, and conversion-specific checks. After conversion, only the final runtime resources are imported here.

The runtime build uses PS2SDK and gsKit from the configured PS2DEV installation. Their headers, libraries, startup link file, and Makefile fragments are external dependencies and are not copied into this repository.

The supplied TIM2 files are already final runtime textures. The runtime loader intentionally supports the raw single-level CT16 and CT32 layout used by these files and rejects unsupported variants rather than pretending to support every TIM2 layout.

Reference documentation:

- [PS2DEV releases](https://github.com/ps2dev/ps2dev/releases)
- [gsKit project](https://github.com/ps2dev/gsKit)
- [ps2-asset-converter](../ps2-asset-converter/README.md) when both repositories are checked out beside one another
