# Strata Gaps

This file records places where real binary formats expose useful structure that
the current Strata language or semantic layer cannot express cleanly yet.

## Cross-section and index joins

Formats such as WebAssembly split related facts across separate physical
sections. A useful function summary needs to join type indexes, import counts,
export names, debug names, and code-body ordinals. Pure Strata can render each
section honestly, but it cannot synthesize joined rows such as:

```text
func[0] answer : type[0] () -> i32
```

Potential direction: declarative collected facts or a semantic `view(...)`.

## SFNT/OpenType table correlation

SFNT fonts store a table directory with four-character tags, offsets, and
lengths. Pure Strata can render the directory and raw table payloads, but typed
table parsing wants lookup-by-tag over an array of table records:

```text
find table record where tag == "name", then render NAME_TABLE at offset
find table record where tag == "cmap", then render CMAP_TABLE at offset
```

Potential direction: array lookup helpers, keyed collections, or semantic
font-summary support.

## FourCC enums and richer properties

Several formats use four-byte tags (`OTTO`, `ttcf`, `name`, `cmap`, `head`,
MP4 box types, RIFF chunks). Strata now has `format("fourcc")` scalar display and
`fourcc("....")` case helpers, so definitions no longer need to model those as
`[string, count(4)] char tag[]` just to get readable names.

The remaining gap is richer FourCC-aware interpretation: for example PNG chunk
type letters encode ancillary/private/reserved/safe-to-copy properties, and a
scalar FourCC field cannot currently combine `format("fourcc")` display with enum choice
editing in a single polished view.

Potential direction: layered display tags or FourCC-specific property helpers.

## Derived summaries

Summaries such as "WASM Summary" or "Font Tables" are not physical byte ranges.
They should be explicitly modeled as derived semantic views rather than fake
structs, so byte-accurate layout remains honest.

## PNG chunk semantics

PNG chunks are easy to render structurally, but several useful checks are
semantic rather than layout:

- CRC validation needs computed CRC-32 over chunk type + data.
- IDAT payloads are a zlib/DEFLATE stream split across one or more chunks.
- Text chunks (`tEXt`, `zTXt`, `iTXt`) contain NUL-separated variable fields
  followed by remaining payload bytes; pure Strata cannot express "remaining
  bytes after a terminated subfield" cleanly yet.
- Chunk type letter case encodes ancillary/private/reserved/safe-to-copy
  properties; a FourCC-aware display/helper would make this cleaner.

## BMP palette and bitfield payloads

BMP/DIB headers determine follow-on payloads from several fields at once:
header size, bit depth, compression, `colorsUsed`, and the pixel offset in the
outer file header. Pure Strata can select the DIB header variant and expose the
pixel payload at `bfOffBits`, but it cannot yet cleanly express:

- palette entry count as `colorsUsed != 0 ? colorsUsed : (1 << bitCount)` only
  for indexed-color formats;
- RGB bit masks that may either live in V2/V3/V4/V5 headers or immediately after
  a 40-byte BITMAPINFOHEADER when compression is `BI_BITFIELDS`;
- a byte range between "end of selected DIB header" and `bfOffBits` as typed
  palette/mask data without duplicating header-specific arithmetic.

Potential direction: range fields (`bytes_until(offset)`), richer derived
expressions, or semantic image-summary support.

## ICO/CUR embedded image dispatch

ICO/CUR directory entries point at image payloads elsewhere in the file. Modern
icons commonly embed PNG images, while older entries store DIB-style bitmap
data without a BMP file header. Pure Strata can render the directory and raw
bounded image bytes, but it cannot dispatch a dynamic payload type based on the
bytes found at that target offset.

Potential direction: dynamic payload `select_offset(offset, ...)`, reusable
subfile views, or semantic icon-summary support.

## GIF packed field display and LZW image data

GIF packs several independent values into bytes in the Logical Screen Descriptor,
Image Descriptor, and Graphic Control Extension. Pure Strata can use bit
expressions to size optional color tables and render the block stream, but it
does not currently split those packed bytes into named bitfield sub-values.

GIF image data is also LZW-compressed in a chain of data sub-blocks. Structure
View can show the sub-block boundaries honestly, but decoding pixels would need
semantic image support.

Potential direction: declarative packed-bitfield display and optional semantic
image/frame summaries.

## WOFF decompression and WOFF2

WOFF 1.0 wraps sfnt tables with optional zlib compression. Pure Strata can render
the WOFF header, table directory, and compressed table byte ranges, but it cannot
inflate compressed tables and then reuse the SFNT/OpenType table definitions on
the decompressed stream.

WOFF2 is a separate format (`wOF2`) with a variable-length table directory and
Brotli-compressed transformed font data. Modeling it cleanly needs either new
variable integer types (`UIntBase128`, `255UInt16`) plus Brotli-aware semantic
support, or a dedicated semantic parser.

Potential direction: decompressed subfile views and reusable transformed-font
semantic support.

## ISO BMFF / MP4 recursive boxes and sample semantics

ISO Base Media files are recursive box trees: `moov` contains `trak`, `trak`
contains `mdia`, and so on. Pure Strata can render box headers, FourCC labels,
bounded payloads, and a limited number of explicit container levels, but it does
not have a reusable recursive "box contains boxes until this extent is exhausted"
construct.

Useful reverse-engineering views also need to join sample tables (`stco`/`co64`,
`stsc`, `stsz`, `stts`, `ctts`, `stss`) into derived media extents and timelines.
That is semantic data synthesized from several boxes rather than a physical
layout field.

Potential direction: recursive type support or a box-list helper, FourCC-aware
selection/display, and semantic sample-table summaries.

## RIFF profile dispatch and recursive chunk lists

RIFF-family files share the same chunk envelope but assign chunk meaning by both
form type and nested list type. Pure Strata can render generic RIFF/WAVE/WebP
roots, FourCC chunk labels, even-byte chunk padding, and selected known payloads,
but it cannot yet express profile-scoped dispatch such as "`fmt `" only for WAVE
or `strf` differently for AVI stream kinds.

RIFF `LIST` chunks can also contain nested chunks whose interpretation depends
on the list FourCC. Modeling this cleanly wants reusable recursive chunk-list
support or a "render chunks until this extent is exhausted" helper.

File detection has a related limitation: Structure View metadata can list
several `magic(...)` signatures, but they are alternatives rather than a compound
predicate. A precise WebP detector wants `RIFF` at offset 0 and `WEBP` at offset
8, not either signature independently.

Potential direction: profile-aware union dispatch, recursive bounded arrays, and
compound `magic` predicates.

## TAR/GZip archive semantics

TAR headers store numeric fields as fixed-width ASCII octal text. Strata now has
`octal(text)` so the basic entry payload extent can be modeled honestly, but
full archive semantics still need more than raw layout:

- PAX/GNU long-name and long-link records apply metadata to the following entry;
  pure Strata renders those records physically but cannot merge the derived name
  into the next entry row.
- Sparse-file maps and vendor-specific typeflags need profile-aware parsing.
- GZip can render its header, optional filename/comment fields, compressed byte
  stream, and trailer, but it cannot inflate DEFLATE data or expose the wrapped
  subfile (for example a `.tar.gz`) as a nested Structure View.

Potential direction: derived archive-entry views, decompressed subfile views, and
profile-specific TAR extension handling.

## CAB folder streams and cabinet sets

Windows Cabinet files separate file metadata (`CFFILE`) from compressed folder
streams (`CFFOLDER`/`CFDATA`). Pure Strata can render the header, folder table,
file table, and raw compressed data blocks, but useful archive browsing needs
semantic joins:

- `CFFILE.iFolder` and `uoffFolderStart` need correlation with folder streams to
  show which data range belongs to each file.
- MSZIP, Quantum, and LZX folder streams need decompression before file payloads
  can be shown.
- Header-level reserve sizes affect per-folder and per-data-block reserve bytes;
  modeling those cleanly wants parameterized/reusable structs or profile-aware
  parsing.
- Multi-part cabinet sets link previous/next cabinet and disk names across
  separate files.

Potential direction: decompressed folder-stream views, archive-entry summaries,
and cross-file cabinet-set metadata.

## Raw disk images, GPT, and filesystems

Raw `.img`/`.dd` disk images can start with an MBR, but useful OS-distro views
quickly become layered:

- Protective MBR entries should lead to the GPT header and partition-entry array
  at later LBAs.
- Partition payloads need dispatch by MBR type or GPT partition GUID.
- Nested filesystem views such as FAT, ISO9660, SquashFS, and ext* need subfile
  parsing over partition byte ranges.
- Sector size is currently assumed to be 512 bytes in `rawimg.strata`.

Potential direction: partition-map semantic views, GUID helpers, subfile views,
and filesystem-specific Strata definitions.

## ISO 9660, boot metadata, and filesystem extensions

ISO images have a straightforward volume-descriptor area, but full distro-image
navigation needs several layers beyond raw descriptor layout:

- Directory records form variable-length lists inside directory extents; pure
  Strata can expose the pointed-to extent bytes, but it cannot recursively parse
  records until the directory extent is exhausted.
- El Torito boot catalogs are referenced from boot-record descriptors and need
  checksum validation plus boot-entry decoding.
- Joliet supplementary descriptors change filename encoding to UCS-2, and Rock
  Ridge stores POSIX metadata in System Use fields.
- Hybrid ISO images may also contain MBR/GPT/APM partition maps before or around
  the ISO 9660 filesystem.

Potential direction: bounded variable-record arrays, recursive directory views,
UCS-2 string display, checksum helpers, and cross-format subfile dispatch.

## Java class semantic resolution

Java class files are structurally straightforward, but useful navigation needs
semantic interpretation of the constant pool:

- `CONSTANT_Long` and `CONSTANT_Double` occupy two constant-pool indexes even
  though only one `cp_info` structure is present. Pure Strata renders physical
  entries honestly but cannot decrement the remaining logical index count or
  insert a synthetic skipped slot.
- Class, field, method, descriptor, and attribute names are indexes into the
  constant pool. A readable class summary needs lookup-by-index and UTF-8
  decoding, including Java's modified UTF-8 details.
- Attribute payloads such as `Code`, `LineNumberTable`, and annotations need
  string-keyed dispatch through the attribute-name constant.

Potential direction: indexed collection lookup helpers, derived class summaries,
and attribute-name based payload dispatch.

## Mach-O universal binaries and dyld metadata

The thin Mach-O layout can be represented in Strata with endian-sensitive
headers and typed load commands. Several useful reverse-engineering views still
need semantic support:

- Universal/fat Mach-O starts with `CA FE BA BE`, which collides with Java class
  file magic. Precise auto-detection wants compound predicates using both magic
  and surrounding header plausibility.
- Symbol tables, indirect symbols, exports, rebases, binds, and chained fixups
  need joins between multiple load commands and string tables.
- Fat/universal binaries need subfile views for each architecture slice.

Potential direction: compound magic predicates, indexed string-table helpers,
dyld metadata semantic views, and nested subfile views.
