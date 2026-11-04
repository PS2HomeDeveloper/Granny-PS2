# Granny PS2

A PlayStation 2 runtime foundation for the Granny character and final PS2 resources.

This repository contains the game/runtime project only. The FBX converter and source FBX files are maintained separately in `ps2-modl-converter`.

## Current development build

The project intentionally has one runtime output:

```text
build/output/usb/Granny.elf
```

`Granny.elf` contains one embedded `DATA.PAK` blob. The mesh, skeleton, animation, material, TIM2 textures, and RAW font sheets are packed first and then the single package is embedded into the ELF. The font sheets are ordinary package resources; they are not generated as separate C arrays or linked as separate objects. There is no external package loader, USB file loading, IOP module loading, ISO generator, or ISO output in this development version.

## Source and build tree

Public headers are kept directly in `include/`. Private implementation headers stay
inside `src/`, next to the subsystem that owns them.

```text
include/
├── assets.h
├── dbglog.h
├── dev_hud.h
├── font_engine.h
├── model.h
├── player.h
└── renderer.h

src/
├── main.c
├── core/
│   ├── assets.c
│   ├── dbglog.c
│   └── gr_limits.h
├── entities/
│   └── player/
│       └── player.c
├── render/
│   ├── dev_hud.c
│   ├── font_engine.c
│   ├── renderer.c
│   ├── tim2.c
│   └── tim2.h
└── scene/
    └── model/
        ├── model.c
        ├── model_animation.c
        ├── model_internal.h
        └── model_loader.c

resources/
├── 2D/
├── 3D/
│   ├── models/granny/
│   └── textures/granny/
├── audio/
└── font/

build/
├── assets/                 exact copy of resources/
├── package/DATA.PAK        host-generated package before embedding
├── generated/DATA.PAK.c    only generated C source
├── obj/ee/                 only PS2 EE objects
├── bin/Granny.elf
└── output/usb/Granny.elf
```

## Build

Use a configured PS2DEV environment with PS2SDK, gsKit, Python 3:

```sh
make
```

Useful targets:

```sh
make usb       # build the single embedded USB ELF
make assets    # copy resources, create DATA.PAK, and generate embedded C
make clean
```

`pack_resources.py` collects the final runtime formats, including `.raw`, from the copied resource tree. `bin2c` is called once by the Makefile for the completed `DATA.PAK`; it does not process the font sheets separately.

The Makefile never invokes the separate FBX converter. Update the final resources under `resources/` when a new conversion is ready.

## Runtime

At startup, the runtime validates the embedded GPAK header and index, then exposes all entries through `assets_find()`. The model loader obtains the Granny resources from that package, and the development HUD obtains `font1.raw` and `font2.raw` from the same package before uploading them to VRAM.

The HUD shows FPS, frame count, player position, camera angles, and model counts. Press R2 to toggle it. The runtime also writes periodic startup and frame-state records to `debug_log.txt` when the active device allows file creation.

ISO creation and external DATA.PAK loading are intentionally postponed until the game runtime is ready for that integration work.

## License

Project source is released under the MIT License. PS2SDK and gsKit are external development dependencies.
