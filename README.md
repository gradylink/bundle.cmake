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
- Every fallible `bundle::` call (`find`, `size`, `load`, `load_text`,
  `extract_to`, `open`) takes an optional trailing `int *ec`. Pass nothing and
  it throws `bundle::error` on failure, same as always. Pass `&ec` and it sets
  `*ec` to a `BUNDLE_ERROR_*` code instead of throwing, which also works when
  exceptions are disabled.
- `BUNDLE_NO_EXCEPTIONS` - define to `1` before including `bundle.hpp` to
  require the `ec` form: calling a fallible method without `ec` then aborts
  instead of throwing. It's set automatically when the compiler has
  exceptions disabled (`-fno-exceptions`, no `/EHsc`), so the header stays
  usable there without defining anything yourself. `bundle::istream::error()`
  and `bundle::archive::iterator::error()` report errors that happen mid-read
  or mid-iteration, where there's no call site to hand an `ec` to.

## Limitations

- `.incbin` requires a GNU-compatible assembler (GCC, Clang, or MinGW) - not
  MSVC's `ml`/`ml64`/`armasm`. When building with MSVC (including `clang-cl`),
  `bundle_add()` automatically falls back to emitting the bundle as a plain C
  byte array instead of using `.incbin`. This avoids the assembler
  requirement but is slower to configure and compile for large bundles.
- Bundles produced by `bundle_add()` are ordinary, non-Zip64, single-disk zip
  files under the hood; reading one with `unzip` works fine, but the runtime
  itself rejects Zip64/multi-disk archives as unsupported (bundles are meant for
  a project's own assets, not arbitrary huge zips).
