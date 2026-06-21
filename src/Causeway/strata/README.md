# Strata Language Reference

Strata is a binary structure definition language. It extends C struct syntax with
annotation tags that describe how binary data should be navigated, decoded, and
displayed. Files use the `.struct` extension.

---

## Quick reference

| Category | Keywords |
|----------|----------|
| Files | [`include`](#comments-and-includes) |
| Types | [`struct`](#structs) · [`union`](#unions) · [`enum`](#enums) · [`typedef`](#type-declarations) |
| Tags | [`tagset`](#tagsets) · [`tags`](#tagsets) |
| Display | [`enum(N)`](#display) · [`name`](#display) |
| Layout | [`offset`](#layout) · [`align`](#layout) · [`endian`](#byte-order) · [`entrypoint`](#layout) · [`extent`](#layout) · [`optional`](#layout) |
| Arrays | [`count`](#arrays) · [`terminated_by`](#arrays) |
| Unions | [`select`](#discriminated-unions) · [`case`](#discriminated-unions) |
| Semantic views | [`dynamic_struct`](#dynamic_struct) · [`dynamic_array`](#dynamic_array) · [`dynamic_container`](#dynamic_container) · [`offset_map`](#offset_map) · [`view`](#view) |
| Export | [`export`](#export-metadata) · [`assoc`](#export-metadata) · [`magic`](#export-metadata) |

---

## Comments and includes

```c
// Line comment
/* Block comment */
```

`include` pulls in another `.struct` file, making all its type and enum
definitions available in the current file. Paths are relative to the including
file. Circular includes are detected and ignored.

```c
include "basetypes.struct";
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

Use `signed` or `unsigned` to override the default:

```c
typedef unsigned dword  DWORD;
typedef signed   word   SHORT;
typedef signed   dword  int, long;
```

Built-in special types: `DOSTIME`, `DOSDATE`, `FILETIME`, `time_t`.

---

## Type declarations

### Structs

Fields are laid out sequentially in file order with no implicit padding — layout
is always byte-packed. Structs can be nested and may carry tag blocks on
individual fields or on the typedef itself.

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

`typedef` can introduce multiple name aliases in one declaration:

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

`select(expr)` on a union evaluates an expression and uses the result to
select which member to render. Each member is tagged with `[case(value)]`; only
the member whose value matches is shown. A member with no `case` tag is always
rendered regardless of the discriminator. (`switch_is` is a legacy alias for
`select`.)

```c
[
  select(OptionalHeader32.Magic)
]
union {
    [case(0x10b)] IMAGE_OPTIONAL_HEADER32 OptionalHeader32;
    [case(0x20b)] IMAGE_OPTIONAL_HEADER64 OptionalHeader64;
};
```

If `switch_is` cannot be evaluated (the discriminator field hasn't been read
yet), the entire union is skipped.

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

```c
byte  Data[16];
char  Name[8];
```

### Flexible arrays

Declare with `[]` and supply the bound via a tag:

```c
[count(Header.Count)]           dword Entries[];
[count(4096), terminated_by(0)] char  Name[];
```

---

## Tags

Tags annotate a field or type with metadata. They appear in `[...]` immediately
before the declaration they modify, and are comma-separated:

```c
[enum(MY_ENUM), offset(Base + RelOffset)]
dword EntryPoint;
```

A tag block before a `typedef` applies to the type as a whole.

### Tagsets

A tagset is a named, reusable block of tags, defined at the top level and applied
to a field with `tags(Name)`. The tags are expanded inline as if written directly
on the field.

```c
tagset PE_DATA_DIRECTORY_TAGS
[
    name(IMAGE_DIRECTORY),
    dynamic_struct(IMAGE_DIRECTORY_ENTRY_EXPORT, IMAGE_EXPORT_DIRECTORY,
                   VirtualAddress, Size != 0),
    dynamic_array(IMAGE_DIRECTORY_ENTRY_IMPORT, IMAGE_IMPORT_DESCRIPTOR,
                  VirtualAddress, Size / sizeof(IMAGE_IMPORT_DESCRIPTOR),
                  OriginalFirstThunk == 0 && FirstThunk == 0)
];

[tags(PE_DATA_DIRECTORY_TAGS)]
IMAGE_DATA_DIRECTORY DataDirectory[16];
```

---

## Field tags

### Display

| Tag | Effect |
|-----|--------|
| `enum(Name)` | Show the value as a named enum constant |
| `name("label")` | Override the display label with a string literal |
| `name(field)` | Use the value of `field` as the display label |

```c
[enum(ELF_TYPE)] e16 e_type;
[name(SectionName)] dword Offset;
```

### Layout

| Tag | Effect |
|-----|--------|
| `offset(expr)` | Pin this field or type to a logical offset within the current container (added to the container's base file offset) |
| `align(n)` | Align to an n-byte boundary before this field |
| `extent(bytes)` | Limit parsing of this field to `bytes` bytes |
| `optional(cond)` | Skip this field when `cond` is false |
| `entrypoint(expr)` | Mark the evaluated expression as a code entry point address for disassembly |

```c
[offset(dosHeader.e_lfanew)]
IMAGE_NT_HEADERS ntHeaders;

[optional(FileHeader.SizeOfOptionalHeader != 0),
 extent(FileHeader.SizeOfOptionalHeader)]
IMAGE_OPTIONAL_HEADER OptionalHeader;
```

### Byte order

```
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

### Arrays

| Tag | Effect |
|-----|--------|
| `count(expr)` | Element count for a flexible array (`size_is` is a legacy alias) |
| `terminated_by(val)` | Stop reading when an element equals `val` |

---

## Semantic views

These tags are placed on a `typedef` to extend how the renderer presents a type —
attaching additional structures, arrays, or named overlays beyond the raw field layout.

| Tag | Effect |
|-----|--------|
| `dynamic_struct(...)` | Render a single struct at a computed offset |
| `dynamic_array(...)` | Render a variable-length array at a computed offset |
| `dynamic_container(Type)` | Create a layout container per array element for the above to attach into |
| `offset_map(va, size, raw)` | Declare how virtual addresses within a container map to file offsets |
| `view("id")` | Attach a named C++ semantic view overlay to a type |

### `dynamic_struct`

Renders a single struct at an offset field, guarded by a condition:

```
dynamic_struct(selector, Type, offset_field, condition)
```

- `selector` — an enum value; only the matching array element gets this struct
- `Type` — the struct type to render at that offset
- `offset_field` — field in the enclosing struct holding the file offset
- `condition` — skip when false

```c
dynamic_struct(IMAGE_DIRECTORY_ENTRY_EXPORT, IMAGE_EXPORT_DIRECTORY,
               VirtualAddress, Size != 0)
```

### `dynamic_array`

Renders a variable-length array at an offset field:

```
dynamic_array(label_or_selector, ElemType, offset_field, count
              [, stop_cond [, condition]])
```

- `label_or_selector` — a field name used as a display label (when on a typedef),
  or an enum selector (when used in a tagset applied to an indexed array)
- `ElemType` — element type
- `offset_field` — field holding the base file offset
- `count` — max element count (expression)
- `stop_cond` — stop early when this per-element expression is true *(optional)*
- `condition` — render only when true *(optional)*

```c
// Label form — on IMAGE_IMPORT_DESCRIPTOR itself:
dynamic_array(DllName, CHAR, Name, 4096, 0)

// Selector form — applied via a tagset to DataDirectory[]:
dynamic_array(IMAGE_DIRECTORY_ENTRY_IMPORT, IMAGE_IMPORT_DESCRIPTOR,
              VirtualAddress, Size / sizeof(IMAGE_IMPORT_DESCRIPTOR),
              OriginalFirstThunk == 0 && FirstThunk == 0)

// With condition — only when the PE is 32-bit:
dynamic_array(ImportLookup32, IMAGE_THUNK_DATA32, OriginalFirstThunk, 512,
              Function == 0, ntHeaders.OptionalHeader32.Magic == 0x10b)
```

### `dynamic_container`

Applied to an array field. Creates one opaque layout container per element;
other `dynamic_array` / `dynamic_struct` declarations resolve their offsets
against these containers.

```c
[dynamic_container(SECTION), ...]
IMAGE_SECTION_HEADER sectionHeader[];
```

### `offset_map`

Declares how virtual addresses within a container map to raw file offsets.
Required when `dynamic_array` / `dynamic_struct` targets use virtual addresses
rather than direct file offsets (e.g. PE sections).

```
offset_map(va_base, size, file_offset)
```

```c
[
  offset_map(VirtualAddress, SizeOfRawData, PointerToRawData),
  dynamic_container(SECTION),
  size_is(ntHeaders.FileHeader.NumberOfSections)
]
IMAGE_SECTION_HEADER sectionHeader[];
```

### `view`

`view("id")` registers a semantic view overlay for a type. When the renderer
encounters an instance of that type it invokes the named C++ view to produce
a human-readable summary alongside the raw fields.

```c
[view("pe.imports")]
typedef struct _IMAGE_IMPORT_DESCRIPTOR { ... } IMAGE_IMPORT_DESCRIPTOR;
```

---

## Export metadata

These tags mark a top-level `typedef` as a file format that Causeway can
detect and open automatically:

| Tag | Effect |
|-----|--------|
| `export` / `export("name")` | Register as a file-format root, with optional human-readable name |
| `assoc(".ext", ...)` | File extensions for this format |
| `magic(offset, { b, b, ... })` | Magic byte sequence at `offset` |

```c
[
  export("Portable Executable (PE)"),
  assoc(".exe", ".sys", ".dll"),
  magic(0, { 'M', 'Z' })
]
typedef struct _PE { ... } PE;
```

---

## Expressions

Tags accept C-like expressions. Fields of the enclosing struct are in scope
by name. Nested fields are accessed with `.`:

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

```c
size_is(Header.Count * sizeof(DWORD))
optional(Signature == 0x00004550)
terminated_by(0)
```

---

## Reserved keywords

These are parsed without error but are not yet implemented — they have no effect
on rendering:

| Keyword | Intended purpose |
|---------|-----------------|
| `bitflag(Name)` | Display a field as named bit flags |
| `display(expr)` | Override the displayed value |
| `ignore` | Parse but hide a field |
| `length_is(expr)` | Byte length of a flexible array |
| `string` | Render bytes as a character string |
| `style(...)` | Rendering style hint |

> None of these are implemented — they parse without error but have no effect.
