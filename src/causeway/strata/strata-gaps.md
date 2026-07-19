# Strata Gaps

This file records useful binary-format behavior that the current Strata
language, renderer, or semantic layer still cannot express cleanly. Completed
capabilities belong in `README.md`, not here.

The raw layout language already supports variable-width scalars, logical array
counts, extent-bounded recursion, terminated fields, named offset spaces,
FourCC display and selection, positional semantic collections, architecture-aware
code ranges, lazily rendered dynamic data, row diagnostics, nested source slices,
and reusable bitfield display schemas. The remaining work is dominated by
cross-table correlation, transformed byte streams, richer derived display, and
format-specific semantics that need lookups rather than physical layout alone.

## Cross-cutting engine gaps

### Declarative semantic views

This is now mostly a strength rather than a core gap. Declarative semantic roots
exist for PE, ELF, WASM, SFNT, WOFF, and ICO. PE and ELF still have older C++
semantic-view implementations, but the default path is declarative and the C++
path is retained mainly as fallback/compatibility.

Remaining work:

  - convert DEX's C++ semantic view to declarative Strata if the language can
    express it cleanly;
  - reduce WASM-specific renderer glue for function/code linkage and
    disassembler targeting where possible;
  - retire PE/ELF C++ semantic fallbacks once declarative coverage and
    performance are trusted;
  - improve semantic-view ergonomics, diagnostics, and profiling.

### Keyed lookup and cross-table correlation

Definitions can address rendered array elements by numeric position with
`field_at(...)`, find the first scalar-keyed element with `index_of(...)`, and
merge semantic collections by key. They still cannot perform compound-key,
string-key, predicate, range, or cached physical lookups.

This affects Java constant-pool resolution, richer MP4 sample-table joins, CAB
file/folder correlation, and Mach-O symbol/string-table metadata. Simple SFNT
and MP4-style scalar table lookups are now expressible with
`field_at(array, index_of(array, keyField, keyValue), resultField)`.

Potential direction: string/compound predicate lookup helpers and reusable
keyed physical collections. Complicated or expensive joins should remain
semantic views rather than turning raw layout evaluation into a general query
engine.

### Nested source and transformed-stream views

`nested(...)` can expose a bounded byte range as another detected format and
the UI can navigate nested sources. `open_as(...)` remains accepted as the
compatibility/action spelling. This covers direct physical slices such as TAR
entries and nested TAR/TAR/WASM cases.

There is still no reusable way to render a decompressed or otherwise transformed
stream with an existing Strata definition.

This blocks embedded ICO PNGs, WOFF table inflation, `.tar.gz` nesting, CAB
folder decompression, fat Mach-O slices, and filesystem views inside disk-image
partitions.

Potential direction: pair `nested(...)` with explicit transform providers for
zlib/DEFLATE, Brotli, LZX, and other codecs. Transformed rows must retain a
clear relationship to their source bytes without pretending that decompressed
offsets are physical file offsets.

### Computed validation and diagnostics

`warn(...)` and `assert(...)` can attach row diagnostics from ordinary
expressions. The remaining gap is computed validation over byte ranges:
PNG CRC-32, ISO/El Torito checksums, archive integrity fields, and
redundant-endian ISO values still render as data without a verified/invalid
state.

Potential direction: bounded checksum helpers and semantic validation
attributes with explicit source extents.

### Formatting and layered display

`format(...)` covers string/ascii/utf8, utf16 variants, FourCC, GUID/UUID,
decimal, hexadecimal, and binary scalar display. Hexadecimal is zero-padded by
default to the scalar byte width and `format("hex", width(N))` can override it.
Binary defaults to width 8 and accepts `format("bin", width(N))` /
`format("binary", width(N))`. Decimal is intentionally unpadded for now.
Built-in `time_t`, `FILETIME`, `DOSDATE`, and `DOSTIME` types render as
timestamps by default. Plain integer fields can opt into the same rendering path
with `format("timestamp", "unix")`, `format("timestamp", "filetime")`,
`format("timestamp", "dosdate")`, or `format("timestamp", "dostime")`.

`bitfield` now covers packed fields whose masks overlap or contain named
sub-ranges, and is used by GIF, MP4, PE, and Mach-O definitions.

Remaining display gaps:

  - enum/bitflag/bitfield style controls;
  - a possible `tree("sealed")` presentation mode for value-like structs whose
    parsed children should not be expandable in the normal tree. This is not the
    same as `tree("collapsed")`: sealed would deliberately remove the expansion
    affordance despite internal child rows. Its usefulness is uncertain unless
    the row has a strong summarized value/comment and some alternate inspect or
    debug path exists for the hidden parsed fields;
  - derived FourCC properties such as PNG ancillary/private/reserved/safe-to-copy
    bits in one polished row.

### Profile-aware dispatch and compound detection

Union selection works from fields and raw probes in the current structure, but
shared envelope formats often need dispatch inherited from an outer profile.
RIFF payload meaning, for example, depends on form type, nested LIST type, and
sometimes AVI stream kind.

Export metadata also treats multiple `magic(...)` declarations as alternative
signatures. It cannot require a compound predicate such as `RIFF` at offset 0
and `WEBP` at offset 8, or distinguish Java classes from fat Mach-O using header
plausibility after their shared `CA FE BA BE` magic.

Potential direction: explicit profile/context propagation for nested dispatch
and conjunctive detection predicates with bounded scalar probes.

### Self-scoped named offset spaces

`offset_map("space", ...)`, `offset("space", expr)`, and
`value_at("space", expr, Type)` work for descendants and later rows. A row
cannot reliably use a named offset map declared by that same row while its own
destination, key, and name expressions are still being evaluated.

Potential direction: a narrow prebind pass for named spaces, without making the
renderer a general multi-pass interpreter.

## Format-specific gaps

### WebAssembly derived interpretation

The declarative WASM summary can combine imported and defined functions, names,
exports, types, and code bodies in one positional function collection. Remaining
work is richer presentation of referenced signatures and instruction bodies.
Disassembly already handles executable code; a higher-level semantic view may
still be appropriate for control-flow and stack interpretation.

### SFNT/OpenType tables

The shipped definition renders the table directory and bounded table payloads.
Typed `name`, `cmap`, `head`, and related tables need lookup-by-tag over directory
records, followed by table-specific semantic interpretation. This primarily
depends on keyed physical lookup; compressed WOFF variants also depend on
transformed-stream views.

### PNG chunks

The raw chunk stream and known payload layouts are representable. Terminated
text fields followed by remaining payload bytes can now be modeled with
`terminated_by(...)` and `extent_of(...)`; adding `tEXt`, `zTXt`, and `iTXt`
layouts is definition work.

The genuine remaining gaps are CRC validation, FourCC property presentation,
and joining/decompressing split IDAT data into an image stream.

### BMP palettes and bitfield payloads

Ternary expressions, shifts, and `optional(...)` can express indexed palette
counts such as `colorsUsed != 0 ? colorsUsed : (1 << bitCount)`. Adding the
ordinary palette cases is definition work.

The awkward case is typing the range between the selected DIB header and
`bfOffBits`: RGB masks may live inside V2/V3/V4/V5 headers or immediately after
a 40-byte BITMAPINFOHEADER. Expressing that without duplicated variant-specific
arithmetic may warrant a range-to-offset helper or a semantic image summary.

### ICO/CUR embedded image dispatch

Directory entries and bounded image payloads are covered. Dispatching a target
payload as PNG or headerless DIB based on bytes at that dynamic target still
needs a subfile view or dynamic target-type selection.

### GIF images

The block stream, optional color tables, and LZW sub-block boundaries are
covered. Named packed-field slices need richer display support, while decoded
frames require LZW transformation and image/frame semantics.

### WOFF and WOFF2

WOFF 1.0 headers, table directories, and compressed byte ranges are covered.
Inflating tables and reusing SFNT definitions needs transformed-stream views.

WOFF2 additionally needs `UIntBase128` and `255UInt16` scalar decoding plus
Brotli and transformed-font reconstruction, or a dedicated semantic parser.

### ISO BMFF / MP4

Extent-bounded box recursion covers the common movie/sample-table hierarchy,
fragmented-media containers, metadata/item-property containers, and full-box
container prefixes. Broader typed box coverage remains definition work.

Useful media views must join `stco`/`co64`, `stsc`, `stsz`, `stts`, `ctts`, and
`stss` into derived sample extents and timelines. That depends on cross-table
correlation and is likely best presented as a semantic summary.

### RIFF families

Generic RIFF/WAVE/WebP roots, FourCC labels, even-byte padding, selected payloads,
and recursive bounded LIST streams are covered. Remaining work is profile-aware
payload dispatch and precise compound detection, especially for AVI and WebP.

### TAR and GZip

TAR entry extents and ASCII-octal sizes are covered. PAX/GNU long names apply to
the following entry, sparse maps vary by profile, and derived names therefore
need archive-entry semantics.

GZip headers, optional strings, compressed bytes, and trailers are covered.
Inflating DEFLATE and presenting a wrapped format such as TAR needs transformed
subfile support.

### Cabinet archives

CAB headers, folder/file tables, and raw CFDATA blocks are covered. Remaining
work includes correlating `CFFILE` entries with folder streams, handling reserve
sizes cleanly, decompressing MSZIP/Quantum/LZX, and linking multipart cabinet
sets. These require keyed joins, transformed streams, and in the multipart case
cross-file metadata.

### Raw disk images and partitioned filesystems

The shipped raw-image definition covers an MBR and referenced partition bytes.
Remaining work includes GPT headers and entry arrays, dispatch by MBR type or GPT
partition GUID, non-512-byte sector assumptions, and nested filesystem views for
FAT, ISO 9660, SquashFS, ext*, and similar formats.

### ISO 9660 extensions and boot metadata

Volume descriptors, byte-bounded directory streams, logical-block padding,
multi-sector directories, recursive subdirectories, and `.`/`..` cycle
suppression are covered.

Remaining work is El Torito catalog decoding and validation, Joliet UCS-2 names,
Rock Ridge System Use semantics, and hybrid MBR/GPT/APM navigation. Some payload
layouts are definition work; polished names, checksums, and hybrid navigation
depend on the cross-cutting gaps above.

### Java classes

Constant-pool physical layout and the two-slot contribution of
`CONSTANT_Long`/`CONSTANT_Double` are covered with `count_as(2)`.

Readable class, field, method, descriptor, and attribute names still require
constant-pool lookup and Java modified UTF-8 handling. Typed `Code`,
`LineNumberTable`, annotation, and other attribute payloads also need dispatch
through the resolved attribute-name constant.

### Mach-O and universal binaries

Thin Mach-O headers and typed load commands are covered. Symbols, indirect
symbols, exports, rebases, binds, and chained fixups require load-command and
string-table correlation. Fat slices need bounded subfile views, and reliable
Java/fat-Mach-O auto-detection needs compound predicates.
