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
| Semantic views | [`dynamic_struct`](#dynamic_struct) · [`dynamic_array`](#dynamic_array) · [`dynamic_container`](#dynamic_container) · [`offset_map`](#offset_map) · [`view`](#view) · [tag-argument wrapping](#tag-argument-wrapping) |
| Export | [`export`](#export-metadata) · [`assoc`](#export-metadata) · [`magic`](#export-metadata) |
| Expressions | [`sizeof`](#expressions) · [`find_first`](#byte-pattern-search) · [`find_last`](#byte-pattern-search) · [`select_offset`](#select_offset) |

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

Structure fields are laid out sequentially in file order with no implicit padding - layout is always byte-packed. Various alignment and formatting options are available with Strata tags. Structs can be nested and may carry tag blocks on individual fields or on the typedef itself.

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
`size_is(expr)`, `optional(expr)` and `extent(expr)` all get it too.

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
the `.struct` file is loaded, not silently at render time: the structure
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
[count(4096), terminated_by(0)] char  Name[];
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
    dynamic_struct(IMAGE_DIRECTORY_ENTRY_EXPORT, IMAGE_EXPORT_DIRECTORY,
                   VirtualAddress, Size != 0),
    dynamic_array(IMAGE_DIRECTORY_ENTRY_IMPORT, IMAGE_IMPORT_DESCRIPTOR,
                  VirtualAddress, Size / sizeof(IMAGE_IMPORT_DESCRIPTOR),
                  OriginalFirstThunk == 0 && FirstThunk == 0)
];

// apply the tags to a declaration
[tags(PE_DATA_DIRECTORY_TAGS)]
IMAGE_DATA_DIRECTORY DataDirectory[16];
```

---

## Field tags

### Display

The following 'display' tags can be used to alter the rendered value or name of a field in the structure view:

| Tag | Effect |
|-----|--------|
| `enum(Name)` | Show the value as a named enum constant |
| `name("label")` | Override the display label with a string literal |
| `name(field)` | Use the value of `field` as the display label |

```c
[enum(ELF_TYPE)] e16 e_type;
[name(SectionName)] dword Offset;
[name("Alternative name")] dword Offset;
```

### Layout

Layout tag affect the alignment and positioning of fields:

| Tag | Effect |
|-----|--------|
| `offset(expr)` | Pin the field or type to a logical offset within the current container (added to the container's base file offset) |
| `align(n)` | Align to an n-byte boundary before this field |
| `extent(bytes)` | Limit parsing of this field to `bytes` bytes |
| `optional(cond)` | Skip this field when `cond` is false |
| `entrypoint` | Mark this scalar field's own value as a code entry point address for disassembly |

```c
[offset(dosHeader.e_lfanew)]
IMAGE_NT_HEADERS ntHeaders;

[optional(FileHeader.SizeOfOptionalHeader != 0),
 extent(FileHeader.SizeOfOptionalHeader)]
IMAGE_OPTIONAL_HEADER OptionalHeader;
```

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

In real `elf.struct`, `e_ident` is actually declared only inside each
per-bitness header candidate, not as a field of `_ELF` itself — see
[Discriminators that live inside the candidates themselves](#discriminators-that-live-inside-the-candidates-themselves)
for how that still resolves.

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

```c
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

`selector` and `condition` can also be written wrapped — `case(selector)`,
`optional(condition)` — to make which is which explicit rather than relying
on position. Both forms behave identically; wrapping is purely about
readability. See [Tag-argument wrapping](#tag-argument-wrapping) below.

```c
dynamic_struct(case(IMAGE_DIRECTORY_ENTRY_EXPORT), IMAGE_EXPORT_DIRECTORY,
               VirtualAddress, optional(Size != 0))
```

### `dynamic_array`

Renders a variable-length array at an offset field:

```c
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

`dynamic_array` has more positional ambiguity than `dynamic_struct` — the
first argument is either a label *or* a selector depending on context, and
the trailing arguments are either `stop_cond` or `condition` depending on
how many are present. Each can be wrapped to say which it is, in any order,
and any subset of them — wrapping `terminated_by(...)`/`optional(...)` lets
you supply a condition without a stop condition, which bare positional args
can't express:

```c
dynamic_array(case(IMAGE_DIRECTORY_ENTRY_IMPORT), IMAGE_IMPORT_DESCRIPTOR,
              VirtualAddress, Size / sizeof(IMAGE_IMPORT_DESCRIPTOR),
              terminated_by(OriginalFirstThunk == 0 && FirstThunk == 0))

dynamic_array(ImportLookup32, IMAGE_THUNK_DATA32, OriginalFirstThunk, 512,
              terminated_by(Function == 0),
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
    dynamic_array(name(DllName), CHAR, Name, 4096, terminated_by(0)),
    ...
]
typedef struct _IMAGE_IMPORT_DESCRIPTOR { ... } IMAGE_IMPORT_DESCRIPTOR;
```

#### Tag-argument wrapping

`name(...)`, `case(...)`, `terminated_by(...)` and `optional(...)` can wrap a
single argument inside `dynamic_array`/`dynamic_struct`/`dynamic_container`'s
own argument list, written as `wrapperKeyword(value)` in place of a bare
value. This is parsed directly as part of the tag's argument list — it does
not make `name(...)` etc. into general call-expressions usable anywhere in
the language, only inside these specific tags' own arguments, where the
position of a bare value would otherwise be ambiguous. Wrapping is always
optional: every example above also works with plain positional arguments,
and existing `.struct` files using bare arguments don't need to change.

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
Required when `dynamic_array` / `dynamic_struct` targets alternative address schemes rather than direct file offsets (e.g. PE sections using Relative Virtual Addresses ).

```c
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
by name. Nested fields are accessed with `.` (dot) notation. Pointer dereferencing is not supported:

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
| Byte search | `find_first({ ... })` · `find_last({ ... })` |
| Raw read | `select_offset(byteOffset)` |

```c
size_is(Header.Count * sizeof(DWORD))
optional(Signature == 0x00004550)
terminated_by(0)
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

---

## Reserved keywords

Certain reserved keywords are parsed without error but are not yet implemented — they have no effect on rendering:

| Keyword | Intended purpose |
|---------|-----------------|
| `bitflag(Name)` | Display a field as named bit flags |
| `display(expr)` | Override the displayed value |
| `ignore` | Parse but hide a field |
| `length_is(expr)` | Byte length of a flexible array |
| `string` | Render bytes as a character string |
| `style(...)` | Rendering style hint |
