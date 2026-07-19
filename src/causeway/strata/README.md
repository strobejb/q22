# Strata Language Reference

Strata is a binary structure definition language. It extends C struct syntax with
annotation tags that describe how binary data should be navigated, decoded, and
displayed. Structure-definition files use the `.strata` extension and should be
stored in `~/.config/catch22/q22/strata`.

View online: [Strata Language Reference](https://github.com/strobejb/q22/blob/main/src/causeway/strata/README.md)

---

## Quick reference

| Category | Keywords |
|----------|----------|
| Files | [`include`](#comments-and-includes) |
| Types | [`struct`](#structs) · [`union`](#unions) · [`enum`](#enums) · [`typedef`](#type-declarations) |
| Reusable tags | [`tagset`](#tagsets) · [`tags`](#tagsets) |
| Presentation | [`enum(Name)`](#display) · [`bitflag(Name)`](#display) · [`bitfield(Name)`](#display) · [`format("...")`](#display) · [`name`](#display) · [`string`](#display) · [`tree("...")`](#tree-presentation) · [`warn`](#display) · [`assert`](#display) |
| Layout | [`offset`](#layout) · [`align`](#layout) · [`pad_to`](#layout) · [`endian`](#byte-order) · [`entrypoint`](#layout) · [`code`](#layout) · [`nested`](#layout) / [`open_as`](#layout) · [`extent`](#layout) · [`optional`](#layout) |
| Arrays | [`count`](#arrays) · [`max_count`](#arrays) · [`count_as`](#arrays) · [`terminated_by`](#arrays) · [`terminator`](#arrays) |
| Unions | [`select`](#discriminated-unions) · [`case`](#discriminated-unions) |
| Dynamic/semantic views | [`semantic`](#semantic-and-emit) · [`emit`](#semantic-and-emit) · [`emit_node`](#semantic-and-emit) · [`emit_row`](#semantic-and-emit) · [`append`](#positional-semantic-collection-addressing) · [`item`](#positional-semantic-collection-addressing) · [`dynamic_struct`](#dynamic_struct) · [`dynamic_array`](#dynamic_array) · [`dynamic_container`](#dynamic_container) · [`offset_map`](#offset_map) |
| Export | [`export`](#export-metadata) · [`category`](#export-metadata) · [`version`](#export-metadata) · [`assoc`](#export-metadata) · [`magic`](#export-metadata) |
| Expressions | [`sizeof`](#expressions) · [`file_size`](#expressions) · [`extent_of`](#expressions) · [`array_index`](#expressions) · [`element_value`](#expressions) · [`current_offset`](#expressions) · [`str`](#expressions) · [`cstr`](#expressions) · [`concat`](#expressions) · [`fmt`](#expressions) · [`octal`](#expressions) · [`find_first`](#byte-pattern-search) · [`find_last`](#byte-pattern-search) · [`index_of`](#value_at) · [`select_offset`](#select_offset) · [`value_at`](#value_at) |

---

## Comments and includes

Standard C-style line and block-level comments are supported:

```c
// Line comment
/* Block comment */
```

Additional Strata files can also be included with the `include` keyword; this pulls
in another `.strata` file, making all its type and enum definitions available in the current file.
Paths are relative to the including file. Circular includes are detected and ignored.

```c
include "basetypes.strata";
```

---

## Primitive types

| Type | Width | Notes |
|------|-------|-------|
| `byte` | 8-bit | unsigned by default |
| `word` | 16-bit | unsigned by default |
| `dword` | 32-bit | unsigned by default |
| `qword` | 64-bit | unsigned by default |
| `char` | 8-bit | character |
| `wchar_t` | 16-bit | wide character |
| `float` | 32-bit | IEEE 754 |
| `double` | 64-bit | IEEE 754 |
| `uleb128` | 1-10 bytes | unsigned LEB128 variable-width integer |
| `sleb128` | 1-10 bytes | signed LEB128 variable-width integer |

Use `signed` or `unsigned` to override the default:

```c
typedef unsigned dword  DWORD;
typedef signed   word   SHORT;
typedef signed   dword  int, long;
```

Built-in special types: `DOSTIME`, `DOSDATE`, `FILETIME`, `time_t`. Structure
View renders these as timestamps by default: `time_t` and `FILETIME` are shown
as UTC date-times, `DOSDATE` as a date, and `DOSTIME` as a time.

`uleb128` and `sleb128` are scalar integer types whose byte length is decoded
from the file at render time. They advance layout by the encoded byte count and
can be referenced from expressions such as `count(...)` after they have been
read:

```c
uleb128 Size;
[count(Size)]
byte Payload[];
```

---

## Type declarations

### Structs

Structure fields are laid out sequentially in file order with no implicit padding - layout is always byte-packed. 
Various alignment and formatting options are available with Strata tags. Structs can be nested 
and may carry tag blocks on individual fields or on the typedef itself.

```c
typedef struct _IMAGE_FILE_HEADER {
    word  Machine;
    word  NumberOfSections;
    dword TimeDateStamp;
    dword PointerToSymbolTable;
    dword NumberOfSymbols;
    word  SizeOfOptionalHeader;
    word  Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;
```

Just like C, the `typedef` keyword can introduce multiple name aliases in one declaration:

```c
typedef dword OFFSET32, RVA, VA;
```

### Unions

All members of a union share the same file offset, and the union's size is that
of its largest member. Without a discriminator, all members are rendered
simultaneously (useful for type-punning views):

```c
typedef union _VALUE {
    dword AsLong;
    byte  Bytes[4];
    float AsFloat;
} VALUE;
```

#### Discriminated unions

`select(expr)` on a union evaluates an integer or string expression and uses the
result to select which member to render. Each member is tagged with
`[case(value)]`; only the member whose value matches is shown. A member with no
`case` tag is always rendered regardless of the discriminator. A `[default]`
member is rendered only when no `case(...)` member matches. (`switch_is` is a
legacy alias for `select`.)

```c
[
  select(OptionalHeader32.Magic)
]
union {
    [case(0x10b)] IMAGE_OPTIONAL_HEADER32 OptionalHeader32;
    [case(0x20b)] IMAGE_OPTIONAL_HEADER64 OptionalHeader64;
};
```

String selectors are useful when a field's interpretation is keyed by a name in
a string table:

```c
[select(cstr_at(StringsOffset + nameoff, 256))]
union {
    [case("model"), string, count(len)] byte model[];
    [default, count(len)] byte raw[];
};
```

If a nested union has no declarator, the selected member is rendered directly in
the parent row rather than under a separate `union name` row:

```c
struct Payload {
    dword kind;
    [select(kind)]
    union {
        [case(1)] byte text[];
        [default] byte raw[];
    };
};
```

If `select` cannot be evaluated (the discriminator field hasn't been read
yet), the entire union is skipped.

#### Discriminators that live inside the candidates themselves

`select(expr)` normally names a field that's already a sibling of the union
(or an ancestor's field) — something readable before the union is reached.
Some formats don't offer that: ELF's class byte (`e_ident[EI_CLASS]`) is part
of *each* per-bitness header struct, not a separate field declared ahead of
the union.

For exactly that shape — the same field, declared identically by every
`case(...)` candidate — a field reference also resolves by checking each
candidate directly, as long as every candidate that declares it agrees on
its position and size. This applies anywhere a field reference is evaluated
this way, not just `select`/`switch_is`: `endian(expr)`, `offset(expr)`,
`count(expr)`, `optional(expr)` and `extent(expr)` all get it too.

```c
[endian(e_ident[EI_DATA] == ELFDATA2MSB)]
typedef struct _ELF
{
    [select(e_ident[EI_CLASS])]
    union {
        [case(ELFCLASS32)] Elf32_Ehdr header32;
        [case(ELFCLASS64)] Elf64_Ehdr header64;
    };
};
```

Here `Elf32_Ehdr` and `Elf64_Ehdr` each declare `byte e_ident[EI_NIDENT];` as
their own first field — `e_ident` is never hoisted out to `_ELF` itself.
`select(e_ident[EI_CLASS])` still resolves, by trying `header32`/`header64`
in turn and confirming they agree on where `e_ident` is and how big it is.

If the candidates disagree — different offset or size for the same field
name — the reference fails to resolve rather than guessing. For anything
that doesn't fit this shape (not a field shared identically across
candidates), `select_offset(byteOffset)` — see [Expressions](#expressions) —
reads a byte directly by position instead, with no field lookup at all.

A reference that resolves neither way — not a sibling, not a consistent
union-candidate field, and not wrapped in `select_offset` — is caught when
the `.strata` file is loaded, not silently at render time: the structure
view's definitions manager reports it as a load error naming the field and
the tag that referenced it.

### Enums


```c
enum COLOR { Red = 0, Green = 1, Blue = 2 };

typedef enum _MY_FLAGS {
    FLAG_A = 0x01,
    FLAG_B = 0x02,
} MY_FLAGS;
```

Bare `enum` (without `typedef`) is valid and common — enum constants are
frequently used as `case` values and tag arguments without needing a type name.

### Fixed-size arrays

Standard fixed-sized arrays work identically to C:

```c
byte  Data[16];
char  Name[8];
```

### Flexible arrays

Variable-sized arrays can be defined with an empty `[]` with the array bounds specified via a tag instead:

```c
[count(Header.Count)]           dword Entries[];
[max_count(4096), terminated_by(0)] char  Name[];
```

Nested flexible arrays are supported for byte/string-table patterns where the
outer array is a sequence of variable-width inner arrays rather than a C-style
rectangular matrix. Comma-separated `count(...)` and `terminated_by(...)`
arguments apply to successive dimensions. Use `_` as a placeholder when a
dimension has no rule for that tag:

```c
[count(1024, 256), terminated_by(_, 0), extent(StringsSize)]
char Strings[][];
```

This models up to 1024 NUL-terminated strings, each capped at 256 characters,
inside a byte extent of `StringsSize`. The `extent(...)` bounds the whole field
and stops the outer array when the table bytes have been consumed.

An extent-bounded array may recursively contain its enclosing structure. This
models chunk and box formats whose container payload is another sequence of the
same record type. The recursive edge must be an array with both `extent(...)`
and a fixed bound, `count(...)`, or `max_count(...)`; direct embedded recursion
is rejected. Use the structure tag name inside its own typedef because the
typedef alias is installed only after the declaration closes:

```c
typedef struct _BOX {
    dword payloadSize;

    [max_count(payloadSize / 8), extent(payloadSize)]
    struct _BOX children[];
} BOX;
```

The renderer stops if a child makes no byte progress and limits recursive type
expansion to protect malformed inputs. `extent(...)` remains authoritative for
where the field following the recursive array begins.

`count_as(expr)` is for formats whose array count is a logical slot count, not
exactly the number of serialized elements. The rendered element still advances
by its actual byte length, but the array loop counter advances by `expr`.

```c
[count(constantPoolCount - 1)]
CP_INFO constantPool[];

typedef struct _CP_INFO {
    byte tag;
    [select(tag)]
    union {
        [case(5), count_as(2)] LongInfo longValue;
        [default] byte raw;
    };
} CP_INFO;
```

---

### Pointer access

Pointer access to array & memory locations is not supported in this version of the Strata language.

## Tags

Tags are used to annotate a field or type with metadata with a structured notation. They appear in `[...]` immediately before the declaration they modify, and are comma-separated:

```c
[enum(MY_ENUM), offset(Base + RelOffset)]
dword EntryPoint;
```

A tag block before a `typedef` applies to the type as a whole. 

### Tag Sets

A tagset is a named, reusable block of tags, defined at the top level and applied
to a field with the `tags(Name)` keyword. The tags are expanded inline as if written directly on the field.

```c
// define a set of reusable tags
tagset PE_DATA_DIRECTORY_TAGS
[
    name(IMAGE_DIRECTORY),
    dynamic_struct(case(IMAGE_DIRECTORY_ENTRY_EXPORT),
                   type(IMAGE_EXPORT_DIRECTORY),
                   offset(VirtualAddress),
                   mapper(offset_map),
                   optional(Size != 0)),
    dynamic_array(case(IMAGE_DIRECTORY_ENTRY_IMPORT),
                  type(IMAGE_IMPORT_DESCRIPTOR),
                  offset(VirtualAddress),
                  count(Size / sizeof(IMAGE_IMPORT_DESCRIPTOR)),
                  mapper(offset_map),
                  terminated_by(OriginalFirstThunk == 0 && FirstThunk == 0))
];

// apply the tags to a declaration
[tags(PE_DATA_DIRECTORY_TAGS)]
IMAGE_DATA_DIRECTORY DataDirectory[16];
```

---

## Presentation tags

### Display

The following tags change rendered values, labels, diagnostics, or tree shape in
Structure View. They do not change byte layout unless explicitly stated
elsewhere.

| Tag | Effect |
|-----|--------|
| `enum(Name)` | Show the value as a named enum constant |
| `bitflag(Name)` | Show the value as named bit masks and expand the row to list active flags |
| `bitfield(Name)` | Expand a packed scalar with a reusable `bitfield` schema |
| `format("fourcc")` | Show a 4-byte scalar as an ASCII FourCC read from the file bytes |
| `format("string")`, `format("ascii")`, `format("utf8")` | Render a byte or char array as a string preview |
| `format("utf16")`, `format("utf16le")`, `format("utf16be")` | Render a byte array as a UTF-16 string preview |
| `format("guid")`, `format("uuid")` | Render a 16-byte array as a canonical GUID/UUID string |
| `format("hex"[, width(N)])` | Force zero-padded hexadecimal integer display for this row only; default width is the scalar byte width |
| `format("bin"[, width(N)])`, `format("binary"[, width(N)])` | Force zero-padded binary integer display for this row only; default width is 8 |
| `format("dec")` | Force decimal integer display for this row only; decimal is not padded |
| `format("timestamp"[, "unix" \| "filetime" \| "dosdate" \| "dostime"])` | Render an integer as a timestamp; the default is Unix seconds |
| `name("label")` | Override the display label with a string literal |
| `name(field)` | Use the value of `field` as the display label |
| `string` | Legacy sugar for `format("string")` |
| `warn(cond[, "message"])` | Add a warning diagnostic to the row when `cond` is true |
| `assert(cond[, "message"])` | Add an error diagnostic to the row when `cond` is false |

```c
[enum(ELF_TYPE)] e16 e_type;
[bitflag(ELF_SECTION_FLAGS)] e64 sh_flags;
[bitfield(GIF_LOGICAL_SCREEN_PACKED_FIELDS)] byte packedFields;
[name(SectionName)] dword Offset;
[name("Alternative name")] dword Offset;
[format("fourcc")] dword Tag;
[format("hex", width(8))] dword Rva;
[format("bin")] byte PackedFlags;
[format("timestamp", "unix")] dword TimeDateStamp;
[string] byte Name[32];
[format("utf16le")] byte WideName[64];
[assert(element_value() == fourcc("RIFF"), "Expected RIFF magic")] dword Magic;
[warn(element_value() > file_size(), "Size extends past end of file")] dword Size;
```

Reusable bitfield display schemas are file-level declarations. They do not
change layout; they only describe child rows for scalar fields tagged with
`bitfield(Name)`.

```c
enum GIF_PACKED_MASKS
{
    GlobalColorTableSize = 0x07,
    SortFlag             = 0x08,
    ColorResolution      = 0x70,
    GlobalColorTableFlag = 0x80
};

bitfield GIF_LOGICAL_SCREEN_PACKED_FIELDS
{
    field("GlobalColorTableSize", GlobalColorTableSize);
    match(SortFlag);
    field("ColorResolution", ColorResolution);
    match(GlobalColorTableFlag);
};
```

`match(mask)` is shorthand for `(value & mask) == mask`, intended for named
single-bit flags. `match(name, mask) = value` tests an exact masked value.
`field(name, mask[, enum(ValueEnum)])` extracts the masked bits and displays the
result, shifting contiguous masks down before optional enum lookup.

`format("time_t")`, `format("unix")`, `format("filetime")`,
`format("dosdate")`, and `format("dostime")` are accepted timestamp aliases,
but `format("timestamp", "...")` is the clearest spelling for new definitions.

### Tree presentation

`tree("...")` changes only how parsed rows and semantic schema groups appear in
Structure View. It does not change layout, expression evaluation, or byte
consumption.

| Tag | Effect |
|-----|--------|
| `tree("hidden")` | Parse and lay out the field normally, but suppress its row |
| `tree("collapsed")` | Show an expandable row initially closed |
| `tree("expanded")` | Show an expandable row initially open |
| `tree("flatten")` | Suppress the wrapper row and promote its children |

There is no separate `inline` keyword today; use `tree("flatten")`. `flatten`
is intentionally about tree presentation: the wrapper row is suppressed and its
children are promoted. An `inline` alias can be added later if we want a softer
authoring spelling, but `inline` should not replace `flatten` abruptly because
it can be confused with physical inline layout.

`sealed` display is not implemented yet. If added, it would mean the row cannot
be expanded even though the renderer may have parsed child items internally.
That is intentionally different from `tree("collapsed")`, which still allows
the user to inspect children. The open design question is whether this is useful
enough: it only makes sense for value-like structs with a strong summarized
display value, and may need an alternate debug/inspect path so parsed child data
is not permanently hidden.

## Layout tags

### Layout

Layout tags affect the alignment and positioning of fields:

| Tag | Effect |
|-----|--------|
| `offset(expr)` | Pin the field or type to a logical offset within the current container (added to the container's base file offset) |
| `offset("space", expr)` | Render the row at bytes resolved through a named `offset_map` space |
| `align(n)` | Align to an n-byte boundary before this field |
| `extent(bytes)` | Limit parsing of this field to `bytes` bytes |
| `pad_to(n)` | Pad this field's consumed extent so the following field starts on an n-byte boundary |
| `optional(cond)` | Skip this field when `cond` is false |
| `entrypoint` | Mark this scalar field's own value as a code entry point address for disassembly |
| `code("arch" \| architecture(field)[, offset(expr), extent(expr)])` | Mark a byte range as code for the disassembler. `architecture(field)` obtains the Capstone id from the matching enum member's `[architecture("...")]` metadata. |
| `nested(type(RootType \| auto), offset(expr), extent(expr)[, name(expr)])` | Mark the row as a navigable physical slice that can be opened as another Strata root; `open_as(...)` is a compatibility alias |

```c
[offset(dosHeader.e_lfanew)]
IMAGE_NT_HEADERS ntHeaders;

[optional(FileHeader.SizeOfOptionalHeader != 0),
 extent(FileHeader.SizeOfOptionalHeader)]
IMAGE_OPTIONAL_HEADER OptionalHeader;

[count(len), pad_to(4)]
byte Data[];

[code("wasm")]
byte instructions[];

[nested(type(MACHO), offset(offset), extent(size),
         name(fmt("Mach-O slice {0}", cputype)))]
typedef struct _FAT_ARCH {
    [enum(CPU_TYPE)] dword cputype;
    dword cpusubtype;
    dword offset;
    dword size;
    dword align;
} FAT_ARCH;

// PE/ELF-style mapped entry point: resolve a scalar logical address through
// a named offset map, and take the architecture from the machine enum.
[entrypoint,
 code(architecture(root::ntHeaders.FileHeader.Machine),
      offset("rva", AddressOfEntryPoint), extent(65536))]
dword AddressOfEntryPoint;
```

`nested(...)` describes a bounded byte range inside the current source. The
first implementation is physical only: `offset(...)` and `extent(...)` select
bytes from the current source. `type(auto)` asks q22 to detect the nested format
from those bytes; `type(RootType)` forces a specific Strata root. `open_as(...)`
remains accepted as a compatibility alias and matches the UI action wording.
Compressed or decoded child sources, such as `tar.gz`, require a later transform
layer.

### Byte order

```c
endian("big") | endian("little") | endian(expr)
```

Controls how multi-byte integers are read. The default is **little-endian**.

When placed on a struct or union, the setting is inherited by all nested fields
and child types — you do not need to repeat it on each field. A nested
`endian(...)` tag overrides the inherited value for that subtree only.

The expression form allows data-driven byte order, where a non-zero result means
big-endian:

```c
// ELF: big-endian when EI_DATA field says so, inherited by all ELF fields
[endian(e_ident[EI_DATA] == ELFDATA2MSB)]
typedef struct _ELF { ... } ELF;

// Literal forms
[endian("big")]    dword NetworkLong;
[endian("little")] dword LittleField;
```

In real `elf.strata`, `e_ident` is actually declared only inside each
per-bitness header candidate, not as a field of `_ELF` itself — see
[Discriminators that live inside the candidates themselves](#discriminators-that-live-inside-the-candidates-themselves)
for how that still resolves.

### Arrays

| Tag | Effect |
|-----|--------|
| `count(expr[, expr...])` | Element count for a flexible array (`size_is` is a legacy alias); extra arguments apply to nested dimensions |
| `max_count(expr[, expr...])` | Safety cap for a flexible array that can stop earlier with `terminated_by(...)` |
| `count_as(expr)` | Make a rendered array element consume `expr` logical count slots instead of one |
| `terminated_by(val[, val...])` | Stop reading when an element equals `val`, when an expression over the rendered element is true, or when a byte sequence such as `{ 0, 0, 1 }` is found; use `_` to skip a nested-array dimension |
| `terminator("hidden")`, `terminator("shown")` | Override whether a matching terminator element is displayed; string-like arrays hide terminators by default, other arrays show them by default |

---

## Semantic and referenced-data views

These tags are placed on a `typedef` to extend how the renderer presents a type —
attaching additional structures, arrays, or named overlays beyond the raw field layout.

After the byte-honest raw structures are defined, a declarative semantic view is
the preferred next step for most user-facing interpretation: summaries, imports,
exports, functions, archive entries, cross-table presentation, and other derived
navigation. Dynamic placement is still useful, but it is specialized raw-tree
machinery for rendering real referenced bytes at computed offsets. The two
approaches can be combined in one definition: use dynamic rows for byte-backed
referenced objects when their physical placement matters, and semantic rows for
the polished view users should normally navigate.

| Tag | Effect |
|-----|--------|
| `semantic(ViewType)` + `emit(...)` | Attach a declarative semantic tree schema and emit byte-backed rows into it |
| `emit_node(...)` | Emit or update a lightweight semantic node with attributes |
| `append(...)` / `item(...)` | Allocate or address `emit_node(...)` rows by position |
| `dynamic_struct(...)` | Render a single referenced struct at a computed offset in the raw tree |
| `dynamic_array(...)` | Render a referenced variable-length array at a computed offset in the raw tree |
| `dynamic_container(type(Type))` | Niche feature: create a layout container per array element for mapped dynamic rows to attach into |
| `offset_map(va, size, raw)` | Declare anonymous virtual-address ranges for `mapper(offset_map)` dynamic rows |
| `offset_map("space", base)` / `offset_map("space", logical, size, raw)` | Define a named offset space for `offset("space", expr)` and `value_at("space", expr, Type)` |

### `semantic` and `emit`

`[semantic]` marks a struct as a semantic-only tree schema. It is not rendered as
raw file layout and may contain unsized destination arrays. Inline structs nested
inside a semantic schema inherit semantic-schema behavior, so the semantic root
can describe the whole summary tree structurally instead of forcing everything
into a flat list of tags. Use `[semantic("Display Name")]` to choose the root
branch label; unlabeled schemas fall back to `Semantic`. The rendered semantic
root is shown under the raw root by default so raw and summary navigation stay
in one tree.
`[semantic(ViewType)]` on a raw root attaches that schema. The raw definition
still comes first and stays byte-honest; the semantic root then defines the
shape of the summary layer, and raw fields use `emit_node(...)`,
`emit_row(...)`, and `emit(...)` to populate that destination tree without
changing the raw layout.

```c
[semantic("WOFF Summary")]
typedef struct _WOFF_VIEW {
    BYTE FontTables[];
} WOFF_VIEW;

[semantic(WOFF_VIEW)]
typedef struct _WOFF {
    [
        count(header.numTables),
        emit(dest(FontTables),
             label(tag),
             type(BYTE),
             offset(offset),
             count(compLength))
    ]
    WOFF_TABLE_DIRECTORY_ENTRY tables[];
} WOFF;
```

`emit(...)` arguments must be wrapped by role:

```c
emit(dest(Path.To.Container), label(expr), type(Type),
     offset(expr) | offset("space", expr),
     count(expr) | max_count(expr)
     [, case(selector)]
     [, map("space", logical_start, size, file_offset)]
     [, terminated_by(stop_condition)]
     [, terminator("hidden" | "shown")]
     [, optional(condition)])
```

- `dest(path)` must resolve to a field in the root's attached semantic schema
- `label(expr)` names each emitted row
- `type(Type)` is the byte-backed type to render at the emitted offset
- `offset(expr)` uses a direct file offset; `offset("space", expr)` resolves through a named `offset_map` space
- `case(selector)` emits only from the matching array element
- `map("space", logical_start, size, file_offset)` attaches an offset map to the emitted semantic row, allowing later `emit(dest(Container.Child), offset("space", expr), ...)` rows to attach under the semantic container whose range owns `expr`
- `count(expr)` / `max_count(expr)`, `terminated_by(...)`, `terminator(...)`, and `optional(...)` mirror dynamic-array behavior

`emit_row(...)` creates or reuses a visible semantic row without rendering a raw
type. It is for semantic entities such as PE section buckets:

```c
emit_row(dest(Path.To.Rows, key(identity_expr), name(display_expr)),
         offset(expr) | offset("space", expr),
         [, map("space", logical_start, size, file_offset)]
         [, case(selector)]
         [, optional(condition)])
```

- `dest(path, key(expr), name(expr))` resolves `path` in the semantic schema,
  finds or creates the row identified by `key(expr)`, and names it with
  `name(expr)`; `key(...)` and `name(...)` are scoped to `dest(...)`
- `key(parent_expr, child_expr)` targets a keyed parent semantic entity in the
  parent destination path, then creates or reuses the keyed child row. This is
  useful for trees such as `Sections.Imports.Functions`, where the DLL row is
  keyed first and the imported function is keyed beneath it.
- without `key(...)`, every `emit_row(...)` appends a new row
- without `name(...)`, the row uses the key value, then the destination name
- `offset(...)`, `map(...)`, `case(...)`, and `optional(...)` behave as they do
  for byte-backed `emit(...)`

`emit_node(...)` creates or updates a lightweight semantic row anchored to the
source bytes, but does not render a raw C type subtree. It is for summaries and
indexes where the raw view already contains the byte-honest structure. Prefer
schema-shaped nodes: the destination array's element type defines the semantic
fields, field order, and default row name, while `emit_node(...)` fills those
fields from raw source expressions.

```c
[semantic("WASM Summary")]
typedef struct _WASM_SUMMARY {
    [name(concat(Module, ".", Name))]
    struct {
        CHAR Module[];
        CHAR Name[];
        BYTE Kind;
        uleb128 TypeIndex;
    } Imports[];
} WASM_SUMMARY;

emit_node(dest(Imports, key(module.bytes, name.bytes)),
          offset(current_offset()),
          field(Module, module.bytes),
          field(Name, name.bytes),
          field(Kind, kind),
          field(TypeIndex, functionTypeIndex))
```

- `dest(path)` appends a new node under a schema destination
- `dest(path, key(expr))` creates or updates the same node for the same key;
  this is semantic merging, not a hidden join table
- `key(parent_expr, child_expr)` targets an existing keyed parent semantic
  entity in the parent destination path, then creates or updates the keyed child
  node. This is useful for trees such as `Sections.Symbols`, where the section
  row is keyed first and the symbol row is keyed beneath it.
- `field(Name, expr)` sets a field declared by the destination element schema;
  unknown field names are diagnosed by definition validation
- schema `name(...)` on the destination element type sets the default visible
  row name using populated semantic fields
- `name(expr)` on `emit_node(...)` sets or overrides the visible row name. Later
  `emit_node(...)` tags only
  replace an existing node name when they provide an explicit non-empty
  `name(...)`.
- `attr(Name, expr)` adds or updates an unstructured child attribute row; use it
  for quick summaries when there is no schema field to fill
- `offset(...)`, `extent(...)`, `case(...)`, and `optional(...)` control source
  anchoring and gating
- `code("arch" \| architecture(field)[, offset(expr), extent(expr)])` marks the semantic node as a
  disassembly target. This lets a semantic function row select the correct
  byte range and Capstone engine, for example `code("wasm")`. For formats
  whose machine value is an enum, annotate supported enum members with
  `[architecture("x86-64")]` and use `architecture(Machine)` instead of
  duplicating the mapping at every code field.
- use `concat(...)` or `fmt(...)` to build display strings directly in the tag

#### Positional semantic collection addressing

`emit_node(...)` can optionally address a destination array by position. This
is useful when several physical tables describe the same ordered semantic
collection:

```c
emit_node(dest(Functions, append("defined")),
          field(TypeIndex, typeIndex))

emit_node(dest(Functions, item("defined", array_index())),
          field(CodeSize, size))

emit_node(dest(Functions, item(functionIndex)),
          field(Name, name.bytes))
```

- `append("sequence")` creates the next row in the complete destination array
  and also records it as the next zero-based item in `sequence`
- `item("sequence", index)` contributes fields and naming to a row previously
  allocated in that sequence
- `item(index)` addresses a zero-based row in the complete destination array
- sequence names must be non-empty string literals; separate declarations may
  append to the same sequence
- `optional(...)` is evaluated before allocation, so filtered sequences remain
  dense
- all successful `append(...)` rows are allocated in source traversal order
  before any `item(...)` contributions are applied; an earlier physical table
  can therefore describe rows allocated later
- missing sequences, failed or negative index expressions, and out-of-range
  indexes emit nothing
- positional addressing is only valid on `emit_node(...)`. A destination that
  uses it cannot also use `key(...)`, ordinary unaddressed node creation,
  `emit(...)`, or `emit_row(...)`

Field merging, schema field order, schema `name(...)`, and explicit node
`name(...)` behave exactly as they do for keyed `emit_node(...)` contributions.

PE/ELF semantic rendering can be isolated when comparing the declarative view
against the older C++ compatibility view:

```sh
Q22_PE_SEMANTIC_VIEW=declarative   # default: skip pe.* C++ semantic interpreters
Q22_PE_SEMANTIC_VIEW=both          # run declarative rows and C++ views
Q22_PE_SEMANTIC_VIEW=cpp           # skip declarative PE semantic rows

Q22_ELF_SEMANTIC_VIEW=declarative  # default: skip elf.* C++ semantic interpreters
Q22_ELF_SEMANTIC_VIEW=both         # run declarative rows and C++ views
Q22_ELF_SEMANTIC_VIEW=cpp          # skip declarative ELF semantic rows
```

When `QEXED_STRUCTURE_PROFILE=1` is set, both declarative semantic rendering and
C++ semantic view invocations are timed in the structure profile log.

Semantic child groups are ordered by their declaration order in the schema, not
by the order in which `emit(...)` tags are encountered while walking raw file
layout. When a mapped semantic container emits byte payload rows to a schema
element whose item type has a `Bytes` child field, those payload rows are grouped
under `Bytes`, so sibling semantic tables can be ordered before or after the raw
bytes in the schema.

Mapped semantic containers model PE-style section ownership declaratively:

```c
[semantic]
typedef struct _PE_SECTION_VIEW {
    IMAGE_IMPORT_DESCRIPTOR Imports[];
    BYTE Bytes[];
} PE_SECTION_VIEW;

[semantic("PE Image")]
typedef struct _PE_VIEW {
    [tree("flatten")]
    PE_SECTION_VIEW Sections[];
} PE_VIEW;

[
  emit_row(dest(Sections, key(Name), name(Name)),
           offset(PointerToRawData),
           map("rva", VirtualAddress, SizeOfRawData, PointerToRawData)),
  emit(dest(Sections.Bytes), type(BYTE),
       offset("rva", VirtualAddress), count(SizeOfRawData))
]
IMAGE_SECTION_HEADER sectionHeader[];

[
  emit(case(IMAGE_DIRECTORY_ENTRY_IMPORT),
       dest(Sections.Imports),
       label("Imports"),
       type(IMAGE_IMPORT_DESCRIPTOR),
       offset("rva", VirtualAddress),
       max_count(Size / sizeof(IMAGE_IMPORT_DESCRIPTOR)),
       terminated_by(OriginalFirstThunk == 0 && FirstThunk == 0),
       terminator("hidden"))
]
IMAGE_DATA_DIRECTORY dataDirectory[];
```

`emit(...)` rows are byte-backed: they render real file bytes at real offsets.
Use `emit_node(...)` and `emit_row(...)` for computed summary nodes, keyed
merging, and attributes.

### `dynamic_struct`

Renders a single referenced struct at a computed offset in the raw tree. Prefer
a semantic view when the goal is a summary or navigation layer; use
`dynamic_struct` when the row should represent real bytes elsewhere in the file.
Arguments must be wrapped by role:

```c
dynamic_struct(type(Type), offset(expr)
               [, name(label) | case(selector)]
               [, container(label)]
               [, mapper(direct | offset_map)]
               [, optional(condition)])
```

- `type(Type)` — the struct type to render
- `offset(expr)` — target file offset expression
- `offset("space", expr)` — target offset resolved through a named `offset_map` space
- `name(label)` — display label when attaching to the current row
- `case(selector)` — only the matching array element emits this struct
- `container(label)` — place the generated dynamic struct under a named root-level group
- `mapper(direct)` — interpret `offset(expr)` as a direct file offset; this is the default
- `mapper(offset_map)` — map `offset(expr)` through anonymous `offset_map(...)` containers before rendering; use this when the generated row should attach to a mapped dynamic container such as a PE section
- `optional(condition)` — skip when false

```c
dynamic_struct(case(IMAGE_DIRECTORY_ENTRY_EXPORT),
               type(IMAGE_EXPORT_DIRECTORY),
               offset(VirtualAddress),
               mapper(offset_map),
               optional(Size != 0))
```

For direct file offsets, omit `mapper(...)` or spell it explicitly:

```c
dynamic_struct(name(LocalFileHeader),
               type(ZIP_LOCAL_FILE_HEADER),
               offset(RelativeOffsetOfLocalHeader))
```

Use `container(Name)` to place generated dynamic rows under a named root-level
group instead of under the owning row:

```c
dynamic_struct(container(RelatedData),
               name(LocalFileHeader),
               type(ZIP_LOCAL_FILE_HEADER),
               offset(RelativeOffsetOfLocalHeader))
```

### `dynamic_array`

Renders a referenced variable-length array at a computed offset in the raw tree.
Prefer a semantic view when the goal is a derived UX summary; use
`dynamic_array` when the row should expose real referenced bytes. Arguments must
be wrapped by role:

```c
dynamic_array(type(ElemType), offset(expr), count(expr) | max_count(expr)
              [, name(label) | case(selector)]
              [, container(label)]
              [, mapper(direct | offset_map)]
              [, terminated_by(stop_condition)]
              [, terminator("hidden" | "shown")]
              [, optional(condition)])
```

- `type(ElemType)` — element type
- `offset(expr)` — target file offset expression
- `offset("space", expr)` — target offset resolved through a named `offset_map` space
- `count(expr)` — exact element count unless stopped early by `terminated_by(...)`
- `max_count(expr)` — safety cap for a terminator-bounded dynamic array; equivalent to `count(...)` at render time, but clearer for sentinel arrays
- `name(label)` — display label; also marks character arrays as a per-element name source
- `case(selector)` — only the matching array element emits this array
- `container(label)` — place the generated dynamic array under a named root-level group
- `mapper(direct)` — interpret `offset(expr)` as a direct file offset; this is the default
- `mapper(offset_map)` — map `offset(expr)` through anonymous `offset_map(...)` containers before rendering; use this when the generated row should attach to a mapped dynamic container such as a PE section
- `terminated_by(stop_condition)` — stop early when this per-element expression is true
- `terminator("hidden"|"shown")` — optional visibility override for the matching terminator element; by default `[string]`/`format("string")` arrays and zero-terminated `char` arrays hide it, while struct/scalar arrays show it
- `optional(condition)` — render only when true

```c
// Direct file-offset form, as used by ZIP central-directory records:
dynamic_array(name(CentralDirectory),
              type(ZIP_CENTRAL_DIRECTORY_FILE_HEADER),
              offset(OffsetOfStartOfCentralDirectory),
              max_count(TotalEntries),
              terminated_by(Signature != ZIP_CENTRAL_DIRECTORY_SIGNATURE),
              terminator("hidden"))

// Mapped selector form, as used by PE data directories:
dynamic_array(case(IMAGE_DIRECTORY_ENTRY_IMPORT),
              type(IMAGE_IMPORT_DESCRIPTOR),
              offset(VirtualAddress),
              max_count(Size / sizeof(IMAGE_IMPORT_DESCRIPTOR)),
              mapper(offset_map),
              terminated_by(OriginalFirstThunk == 0 && FirstThunk == 0),
              terminator("hidden"))

// With condition — only when the PE is 32-bit:
dynamic_array(name(ImportLookup32),
              type(IMAGE_THUNK_DATA32),
              offset(OriginalFirstThunk),
              max_count(512),
              mapper(offset_map),
              terminated_by(Function == 0),
              terminator("hidden"),
              optional(ntHeaders.OptionalHeader32.Magic == 0x10b))
```

`name(...)` wrapping the first argument has a second meaning specific to
`dynamic_array`: it marks *that* array as the per-element name source for
whichever array contains elements of this type. When the structure view
renders such an element, it resolves this array's RVA-redirected content
(eagerly, without needing the array's own row built first) and appends it
to the element's tree label — e.g. `[0] - KERNEL32.dll` instead of just
`[0]`:

```c
[
    dynamic_array(name(DllName), type(CHAR), offset(Name), max_count(4096),
                  mapper(offset_map), terminated_by(0)),
    ...
]
typedef struct _IMAGE_IMPORT_DESCRIPTOR { ... } IMAGE_IMPORT_DESCRIPTOR;
```

### `dynamic_container`

Applied to an array field. Creates one opaque layout container per element;
other `dynamic_array` / `dynamic_struct` declarations resolve their offsets
against these containers. This is a niche feature for mapped raw placement, such
as PE sections: it is not the default way to build a user-facing summary.
Semantic views and dynamic containers can coexist when both are useful.

```c
[dynamic_container(type(SECTION)), ...]
IMAGE_SECTION_HEADER sectionHeader[];
```

### `offset_map`

Declares how logical offsets or virtual addresses map to raw file offsets.
Anonymous three-argument maps support existing `dynamic_array` /
`dynamic_struct` declarations that use `mapper(offset_map)`.

```c
offset_map(va_base, size, file_offset)
```

```c
[
  offset_map(VirtualAddress, SizeOfRawData, PointerToRawData),
  offset_map("rva", VirtualAddress, SizeOfRawData, PointerToRawData),
  dynamic_container(type(SECTION))
]
typedef struct _IMAGE_SECTION_HEADER { ... } IMAGE_SECTION_HEADER;

[
  name(Name),
  count(ntHeaders.FileHeader.NumberOfSections)
]
IMAGE_SECTION_HEADER sectionHeader[];
```

Named maps can also be used directly from `offset(...)` tags and expressions.
Prefer using them from dynamic or semantic rows, where the tree already makes
clear that the rendered bytes are reached by reference. Avoid placing
`offset("space", ...)` fields inline in a normal raw struct when that would make
referenced data look physically adjacent to the previous field. The rendered row
must still point at the true file bytes; the guardrail is about tree meaning, not
address accuracy.

A two-argument named map defines a simple base-relative space:

```c
typedef struct _Header {
    dword stringTableOffset;
    dword nameOffset;
} HEADER;

[
  offset_map("strings", header.stringTableOffset),
  dynamic_array(name(Name), type(CHAR), offset("strings", header.nameOffset),
                max_count(256), terminated_by(0))
]
typedef struct _File { HEADER header; } FILE;
```

A four-argument named map defines range translation:

```c
[
  offset_map("rva", VirtualAddress, SizeOfRawData, PointerToRawData),
  count(sectionCount)
]
IMAGE_SECTION_HEADER sections[];

[offset("rva", importRva)]
IMAGE_IMPORT_DESCRIPTOR imports[];
```

## Export metadata

These tags mark a top-level `typedef` as a file format that Causeway can
detect and open automatically:

| Tag | Effect |
|-----|--------|
| `export` / `export("name")` | Register as a file-format root, with optional human-readable name |
| `category("name")` | Optional Structure View menu section, such as `"code"`, `"image"`, `"media"`, `"font"`, `"storage"`, or `"system"` |
| `version(n)` | Export definition version used to resolve built-in/user duplicates; defaults to `0` |
| `assoc(".ext", ...)` | File extensions for this format; list all extensions in one comma-separated tag |
| `magic({ b, b, ... })` | Magic byte sequence at offset `0` |
| `magic({ b, b, ... }, offset)` | Magic byte sequence at `offset` |

```c
[
  export("Portable Executable (PE)"),
  category("code"),
  version(1),
  assoc(".exe", ".sys", ".dll"),
  magic({ 'M', 'Z' })
]
typedef struct _PE { ... } PE;
```

Use a single comma-separated `assoc(...)` tag for all extensions belonging to an
exported root. Repeating separate `assoc(...)` tags is not canonical and may not
be combined by every consumer.

---

## Expressions

Tags accept C-like expressions. Fields of the enclosing struct are in scope
by name. Nested fields are accessed with `.` (dot) notation. `root::` and
`parent::` switch lookup scope without leaving field syntax. Pointer
dereferencing is not supported:

```
ntHeaders.FileHeader.NumberOfSections
dosHeader.e_lfanew
```

| Category | Operators |
|----------|-----------|
| Arithmetic | `+ - * /` |
| Bitwise | `& \| ^ ~ << >>` |
| Comparison | `== != < > <= >=` |
| Logical | `&& \|\|` |
| Conditional | `? :` |
| Size | `sizeof(Type)` |
| Runtime extent | `extent_of(field)` |
| File size | `file_size()` |
| Array context | `array_index()` |
| Current scalar | `element_value()` |
| Current offset | `current_offset()` |
| Parsed string | `str(field)` |
| String construction | `concat(a, b, ...)`, `fmt("{0}", value)` |
| Octal text | `octal(text)` |
| FourCC literal | `fourcc("abcd")` |
| String lookup | `cstr(offset)`, `cstr("space", offset)`, `cstr_at(offset, maxLen)`, `cstr_from(base, offset[, maxLen])` |
| Byte sequence literal | `{ 0x50, 0x4b }` |
| Byte search | `find_first({ ... })` · `find_last({ ... })` |
| Raw read | `select_offset(byteOffset)`, `value_at(offset, Type)`, `root::value_at(offset, Type)`, `field_at(array, index, field)`, `index_of(array, keyField, keyValue)` |

```c
count(Header.Count * sizeof(DWORD))
optional(Signature == 0x00004550)
terminated_by(0)
terminated_by(kind == 0 && size == 0)
terminated_by({ 0x00, 0x00, 0x01 })
```

`cstr(offset)` reads a NUL-terminated 8-bit string at `offset` relative to the
root structure base, capped at 4096 bytes. `cstr("space", offset)` first
resolves `offset` through a named `offset_map` space. A third argument may
override the cap, still limited to the renderer's maximum string lookup size.
`cstr_at(offset, maxLen)` is the older explicit-cap spelling for root-relative
lookups. `cstr_from(base, offset[, maxLen])` reads from `base + offset`, which
is useful when a table entry stores a string-table-relative offset and another
expression finds that string table's file offset. These return string
expressions, so they are useful with string-valued `select(...)`/`case("...")`
unions and semantic row `key(...)`/`name(...)`.

`concat(...)` converts each argument to display text and appends the parts.
`fmt(...)` takes a string literal template followed by values and replaces
`{0}`, `{1}`, and later placeholders. Both are intended for semantic row
names and attributes.

`file_size()` returns the number of readable bytes from the root structure base.
It is useful for bounding a final stream of records when the format has no
explicit byte-count field:

```c
[count(4096), extent(file_size() - sizeof(Header))]
Record records[];
```

`extent_of(field)` returns the actual rendered byte length of an already-parsed
field. This is useful with variable-width scalars such as `uleb128` and flexible
arrays when a following payload length is relative to the current record:

`array_index()` returns the current rendered array element's zero-based index.
`element_value()` returns the current scalar row's numeric value. They are useful
for parallel table formats where one array element names or indexes data in
another array, such as PE export name and ordinal tables.
`current_offset()` returns the current row's file offset relative to the root
structure. It is useful when an already-rendered raw row wants to emit an
expandable semantic copy of itself.

```c
[extent(size - extent_of(length) - extent_of(name))]
byte payload[];
```

`str(field)` returns the decoded text of an already-parsed string field. The
field must be a `char`/`wchar_t` array or a `byte` array tagged with `[string]`.
It is useful with string-valued `select(...)` unions over length-prefixed names:

```c
[string, count(nameSize)]
byte name[];

[select(str(name))]
union {
    [case("metadata")] Metadata metadata;
    [default] byte raw[];
};
```

`octal(text)` converts a string expression containing ASCII octal digits to an
integer. This is useful for formats such as TAR whose numeric header fields are
stored as NUL/space-padded octal text:

```c
[string, count(12), terminated_by(0)]
char size[];

[count(octal(str(size))), pad_to(512)]
byte data[];
```

`fourcc("abcd")` packs exactly four string-literal bytes into the integer value
that a 4-byte scalar would decode in the current endian context. Pair it with
`format("fourcc")` for FourCC chunk or box discriminators:

```c
[format("fourcc")]
dword type;

[select(type)]
union {
    [case(fourcc("ftyp"))] MP4_FILE_TYPE_BOX fileType;
    [case(fourcc("mdat"))] MP4_MEDIA_DATA_BOX mediaData;
};
```

### Byte pattern search

`find_first(pattern)` and `find_last(pattern)` search for a constant byte
sequence within the current structure instance and return the match offset
relative to that structure's base. The result can be used directly with
`offset(...)`.

```c
[offset(find_last({ 'P', 'K', 0x05, 0x06 }, 65557))]
ZIP_END_OF_CENTRAL_DIRECTORY_RECORD eocd;
```

The optional second argument limits the search to the first or last N bytes of
the current structure scope:

```c
find_first({ 'M', 'Z' }, 1024)          // first 1024 bytes
find_last({ 'P', 'K', 0x05, 0x06 }, 65557) // last 65557 bytes
```

If the pattern is not found, expression evaluation fails; dependent declarations
such as an `offset(...)` field are skipped rather than placed at a sentinel
offset.

### `select_offset`

`select_offset(byteOffset)` reads one raw byte at `byteOffset`, relative to
the current struct/union's own base file offset — no field lookup at all.

This exists for the case the [union-candidate fallback](#discriminators-that-live-inside-the-candidates-themselves)
can't cover: a discriminator that isn't a field shared identically across
every `case(...)` candidate. It bypasses field resolution entirely, so it
always works, at the cost of referring to a byte position instead of a
named field:

```c
select(select_offset(4))
endian(select_offset(5) == 2)
```

Prefer a plain field reference (and the union-candidate fallback) whenever
the discriminator genuinely is the same field in every candidate — it's more
readable, and `select_offset` won't catch a typo'd field name the way a
real field reference will. Reach for `select_offset` only when that
shape doesn't apply. A reserved word, not a tag — `select_offset(...)` only
means something as a sub-expression inside another tag's expression.

### `value_at`

`value_at(offset, Type)` reads a fixed-size scalar at `offset`, relative to
the current structure instance. `value_at("space", offset, Type)` first
resolves `offset` through a named `offset_map` space. `root::value_at(offset,
Type)` uses the same scalar read but makes `offset` relative to the root
structure base; `root_value_at(offset, Type)` remains as a compatibility alias.
This is intended for small one-off probes in expressions, not for rendering
nested structures. `field_at(array, index, field)` reads a field from an
already rendered array element, which is useful for parallel tables and linked
indexes. `index_of(array, keyField, keyValue)` searches an already rendered
array for the first element whose scalar `keyField` equals `keyValue`, returning
that zero-based index for use with `field_at(...)`.

Supported V1 types are `byte`, `char`, `word`, `dword`, `qword`, and
`wchar_t`; multi-byte reads use the current endian context.

```c
[optional(value_at(8, dword) == fourcc("WEBP"))]
RIFF_WEBP_PAYLOAD webp;

[optional(value_at("rva", thunkRva, qword) != 0)]
IMAGE_THUNK_DATA64 thunk;

[name(cstr_from(field_at(sectionHeaders32, sh_link, sh_offset), st_name))]
Elf32_Sym symbol;

[offset(field_at(tableDirectory,
                 index_of(tableDirectory, tag, fourcc("name")),
                 offset))]
SFNT_NAME_TABLE name;
```

---

## Reserved and unsupported keywords

Some keywords are reserved so definitions fail clearly near the unsupported
syntax instead of treating the word as an ordinary identifier.

| Keyword | Intended purpose |
|---------|-----------------|
| `description(...)` | Separate descriptive metadata; use `export("name")` for exported format names today |
| `display(expr)` | Override the displayed value |
| `ignore` | Parse but hide a field; use `tree("hidden")` today |
| `length_is(expr)` | Byte length of a flexible array |
| `style(...)` | Rendering style hint |

## Keyword reference

This list is intended to stay in sync with `src/causeway/keywords.h` and the
Qt Creator highlighter in `scripts/qtcreator/q22-strata.xml`.

| Category | Keywords |
|----------|----------|
| Type declarations | `struct`, `union`, `enum`, `typedef`, `const`, `signed`, `unsigned` |
| Primitive types | `byte`, `word`, `dword`, `qword`, `char`, `wchar_t`, `float`, `double`, `uleb128`, `sleb128` |
| Presentation tags | `assert`, `bitfield`, `bitflag`, `enum`, `format`, `name`, `string`, `tree`, `warn` |
| Presentation argument wrappers | `width` |
| Layout tags | `align`, `architecture`, `code`, `endian`, `entrypoint`, `extent`, `nested`, `offset`, `open_as`, `optional`, `pad_to` |
| Arrays/unions | `case`, `count`, `count_as`, `default`, `max_count`, `select`, `size_is`, `switch_is`, `terminated_by`, `terminator` |
| Dynamic/semantic tags | `dynamic_array`, `dynamic_container`, `dynamic_struct`, `emit`, `emit_node`, `emit_row`, `offset_map`, `semantic` |
| Dynamic/semantic argument wrappers | `append`, `attr`, `container`, `dest`, `field`, `item`, `key`, `label`, `map`, `mapper`, `type` |
| Compatibility/native hooks | `native_view` |
| Export/detection tags | `assoc`, `category`, `export`, `magic`, `version` |
| Top-level/reusable declarations | `bitfield`, `field`, `include`, `match`, `tagset`, `tags` |
| Expression helpers | `array_index`, `concat`, `cstr`, `cstr_at`, `cstr_from`, `current_offset`, `element_value`, `extent_of`, `field_at`, `file_size`, `find_first`, `find_last`, `fmt`, `fourcc`, `index_of`, `octal`, `root_value_at`, `select_offset`, `sizeof`, `str`, `value_at` |
| Reserved/unsupported | `description`, `display`, `ignore`, `length_is`, `style` |

---

## Writing good `.strata` files

Start with the raw file format before adding presentation helpers. A good
definition should let someone compare the structure tree against a hex view and
recognize the on-disk layout.

Recommended workflow:

1. Define primitive aliases, enums, and simple structs first.
2. Add an exported root with `export`, optional `category`, one comma-separated `assoc`, `magic`, and `offset`.
3. Model fields in physical order where possible.
4. Use `offset(...)` for tables or records referenced from elsewhere in the file.
5. Use `count(...)` for exact-count variable arrays, or `max_count(...)` plus `terminated_by(...)` for sentinel-bounded arrays.
6. Use `extent(...)` when a rendered field is capped or conditionally short, but
   its parent still needs to advance by the full byte length.
7. Add display tags (`enum`, `bitflag`, `bitfield`, `format`, `name`) after the
   raw layout is correct.
8. Add a declarative semantic view as the primary next layer when the format
   needs a clearer user-facing tree: summaries, imports, exports, functions,
   archive entries, or cross-table presentation.
9. Add `dynamic_array(...)` or `dynamic_struct(...)` only for byte-backed
   referenced data that should appear in the raw tree at computed offsets.
10. Use `dynamic_container(...)` only for niche mapped-placement buckets, such
    as PE sections, where other dynamic rows should attach to a specific
    physical/virtual range.
11. Add focused Structure View tests for new rendering behavior.

Prefer pure structures for ordinary file layouts. Reach for
`dynamic_array(...)` and `dynamic_struct(...)` only for related data that is not
really an inline C field but still represents real referenced bytes, such as PE
RVA-mapped import/export tables. Treat `dynamic_container(...)` as a narrower
bucket/attachment feature, not a general authoring pattern.

When a format needs a summary view, define the raw layout first and then layer a
single semantic root schema over it. Put the summary structure inside that
semantic root with nested structs and arrays, so the destination shape is
obvious from the definition instead of being inferred from a long run of
`emit(...)` calls. This is not an either/or choice: a definition may use
dynamic rows for byte-backed referenced objects and semantic rows for the
polished navigation tree at the same time.

Named address spaces follow the same rule. Define `offset_map("space", ...)`
where the file format defines the coordinate system, then use
`offset("space", expr)` mainly from dynamic or semantic rows. Do not make an
offset-target field appear as a normal inline child of a raw struct unless that
is deliberately the clearest representation of the format.

When you keep repeating the same table-base probe, hoist it into a named
offset space once and reuse that name for both lookups and display strings.
That is usually cleaner than repeating `field_at(...)` or `cstr_from(...)` in
every tag. Use it when a descendant row will consume the named space; if the
same row needs to use the value for its own key or name, keep the direct probe
for now:

```c
[
  offset_map("shstr", field_at(sectionHeaders32, header32.e_shstrndx, sh_offset)),
  name(cstr("shstr", sh_name))
]
typedef struct _Elf32_Shdr { ... } Elf32_Shdr;
```

Use `enum(Name)` for one-of-N values. Use `bitflag(Name)` only for independent
mask bits. Do not use `bitflag(...)` for packed fields whose values overlap,
such as PE section alignment values. Leave architecture-specific fields raw
unless the definition can switch safely on architecture; for example ELF
`e_flags` is processor-specific and should not use a single generic bitflag
enum.

For large payloads, remember that the Structure View caps how many array
children it renders. Pair `count(...)` with `extent(...)` when layout must
advance by the true byte length:

```c
[count(CompressedSize), extent(CompressedSize)]
byte CompressedData[];
```

For trailer-indexed formats such as ZIP, it is fine to render both the local
records and the central index/trailer records. They represent different parts of
the file even if much of the metadata overlaps.
