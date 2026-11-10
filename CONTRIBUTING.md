# Contributing

Thank you for contributing to Granny PS2. Keep changes focused and preserve the
existing runtime behavior unless the change explicitly intends to modify it.

## C source and header organization

This project separates public interfaces from private implementation details.

### Public headers

Place headers that define interfaces used across subsystems directly in `include/`.

Examples:

- `include/model.h`
- `include/player.h`
- `include/renderer.h`

Use clear, stable names that describe the subsystem or public API. Do not create a
public header for every `.c` file automatically. A subsystem may have several
implementation files while exposing one shared public header.

### Private headers

Place headers used only by an implementation or by files within one subsystem
inside that subsystem's `src/` directory.

Examples:

- `src/scene/model/model_internal.h`
- `src/render/tim2.h`
- `src/core/gr_limits.h`

Keep private structures, internal constants, and helper declarations out of
`include/` unless other subsystems genuinely need them.

### Declarations and functions

- Put declarations in a header only when another translation unit needs them.
- Keep file-local helper functions `static` in their `.c` file.
- Share internal declarations through a private header only when multiple `.c`
  files in the same subsystem need them.
- Do not create empty or speculative headers for systems that do not exist yet.
- Do not expose implementation details merely to make a function accessible.

## Include paths

Public headers are included by their filename, for example:

```c
#include "model.h"
#include "renderer.h"
```

Private headers should be included using the subsystem-relative path supported by
the project's `-Isrc` include path, or a local include when they are in the same
directory.

When moving a source or header file, update all affected `#include` directives and
the build source list if necessary. Keep the Makefile's source and header dependency
tracking in sync with the tree.

## Scope and validation

- Keep unrelated formatting or gameplay changes out of organizational pull
  requests.
- Do not change resource formats or runtime behavior as part of a file move.
- Check for stale duplicate source files after moving or splitting a subsystem.
- Build with the configured PS2SDK environment before submitting when possible,
  and report exactly which build or runtime checks were performed.
