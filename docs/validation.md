# Development Build Notes

The current repository is intentionally limited to one PS2 USB development executable:

```text
build/output/usb/Granny.elf
```

The executable embeds one `DATA.PAK` blob generated from the final resources. The runtime parses that embedded package in memory. No `fileXio`, IOP module, external package, ISO, PAL, or NTSC build path is present.

The build object tree contains only PS2 EE objects under `build/obj/ee/`. Host-side package generation produces `DATA.PAK` and generated C source, not host object files.

External package loading and ISO creation are deliberately postponed. They should be reintroduced later as a separate integration task after gameplay development, with dedicated PS2SDK-based tests and a known-good loader path.

No build, emulator run, or hardware run is claimed for this cleanup change.
