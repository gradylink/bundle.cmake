# bundle.cmake

A CMake helper for embedding folders and files into your binary.

## Why not CMakeRC?

[CMakeRC](https://github.com/vector-of-bool/cmrc) is the usual answer to "embed
some assets in my binary," but it has three gaps this project exists to close:

- No compression.
- C++-only.
- No native stream support.

bundle.cmake supports multiple independent bundles per program (like CMakeRC),
but adds pluggable DEFLATE compression and a real streaming API in both C and
C++.

## API

### CMake

- `bundle_add(<name> [BASE_DIR <dir>] [FILES <file>...] [DIRECTORIES <dir>...] [COMPRESSION STORE|DEFLATE] [OUTPUT_DIR <dir>])`
  Call once per bundle; link the resulting `<name>` target to embed it. Calling
  it again with a different `<name>` embeds another, independent bundle in the
  same program. Virtual paths (what you look entries up by at runtime) are
  always the path of each input relative to `BASE_DIR` (default
  `CMAKE_CURRENT_SOURCE_DIR`).
- `BUNDLE_USE_ZLIB` (CMake option, default `OFF`) - use system zlib instead of
  miniz for DEFLATE decompression.
- Either way, the backend is resolved through [Catalog](https://github.com/catalog-cmake/catalog):
  bundle.cmake fetches and includes it automatically (via a vendored copy of
  its `cl-bootstrap.cmake`) the first time a bundle needs it, unless the
  consuming project already includes its own copy of Catalog first.

### C/C++

- `bundle_iter_begin`/`bundle_iter_next`,
- `bundle_open`/`bundle_read`/`bundle_close` for streaming,
- `bundle_read_all`/`bundle_load` for one-shot reads.
- `bundle::istream`, `extract_to()`, `entries_with_prefix()`.

## Limitations

- `.incbin` requires a GNU-compatible assembler (GCC, Clang, or MinGW) - not
  MSVC's `ml`/`ml64`/`armasm`.
- Bundles produced by `bundle_add()` are ordinary, non-Zip64, single-disk zip
  files under the hood; reading one with `unzip` works fine, but the runtime
  itself rejects Zip64/multi-disk archives as unsupported (bundles are meant for
  a project's own assets, not arbitrary huge zips).
