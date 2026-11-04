# Binary Resource Format

All multi-byte fields are little-endian. Floating-point fields are IEEE-754 32-bit values. Resource sections are aligned to 16 bytes. Files contain no process-local pointers.

## Mesh

`Granny.mesh` starts with `GMSH`, version 1, and a 64-byte header. The header stores vertex count, index count, part count, section offsets, and reference-frame bounds. Each vertex is 48 bytes:

```text
0   float32 position[3]
12  float32 normal[3]
24  float32 uv[2]
32  uint16  joints[4]
40  uint8   weights[4]
44  uint8   reserved[4]
```

Weights sum to 255. Indices are uint32 values and every three indices form a triangle. A part record is 16 bytes: first index, index count, material index, and flags.

## Skeleton

`Granny.skeleton` starts with `GSKL`, version 1. A binding record is 128 bytes:

```text
0    int32   parent index, or -1
4    uint32  source node id
8    char    name[64]
72   float32 geometry_to_bone[3][4]
120  uint8   reserved[8]
```

Bindings are ordered so that parents precede children. Bindings are per mesh instance where required by the source scene; they are not forced to be unique global bones.

## Animation

`Granny.anim` starts with `GANM`, version 1. The header stores binding count, frame count, FPS, duration, matrix data offset, and a 32-byte clip name. Matrix data is stored as `[frame][binding][12]`, using row-major 3x4 affine matrices. Duration is `(frame_count - 1) / fps`; both source endpoints are sampled.

The converter evaluates the complete ufbx scene on the host so that the PS2 runtime does not need to reproduce FBX pivot and inheritance rules. The runtime interpolates between stored matrix samples.

## Materials

`Granny.material` starts with `GMAT`, version 1. A material record is 128 bytes:

```text
0    char    name[48]
48   char    texture filename[48]
96   float32 diffuse RGBA[4]
112  uint32  flags
116  uint8   reserved[12]
```

Flag bit 0 requests transparent blending. Flag bit 1 requests double-sided rendering. An empty texture name means a flat material color.

## Runtime limits

Shared limits are defined in `include/gr_limits.h`: 65,535 vertices, 393,216 indices, 256 bindings, 64 materials, 512 parts, 4,096 frames, 12 MiB of animation matrices, and power-of-two CT16/CT32 textures up to 512x512. The converter and runtime use the same limits.

## DATA.PAK

The runtime stores final resources in an indexed `GPAK` archive before embedding it into the USB ELF. Its 16-byte header is:

```text
0   char    magic[4] = "GPAK"
4   uint32  version = 1
8   uint32  entry count
12  uint32  index offset
```

Each 64-byte index entry contains `char name[48]`, `uint32 data offset`, `uint32 data size`, and eight reserved bytes. Names are resource basenames and must be unique. The USB runtime reads the embedded archive in memory before asking the normal `assets_find()` interface for model and texture resources. `report.json` is deliberately excluded from the archive because it is conversion metadata rather than runtime data.
