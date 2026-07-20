---
name: q22-strata-authoring
description: Write, review, and debug q22 Strata `.strata`/legacy `.struct` binary format definitions. Use when Codex is asked to add or fix files under `src/causeway/strata/`, define binary layouts, add enum/bitflag display tags, model offset-based tables, update Strata docs, or troubleshoot Structure View rendering of PE, ELF, ZIP, or new file formats.
---

# Q22 Strata Authoring

## Workflow

1. Read `AGENTS.md` and `src/causeway/strata/README.md` before changing `.strata` files.
2. Define the raw layout first: fields in physical file order, `offset(...)` for tables elsewhere, `count(...)` for exact-count arrays, `max_count(...)`/`terminated_by(...)` for sentinel-bounded arrays, and `extent(...)` when rendered children are capped but layout must advance by full byte length.
3. After the raw structure is honest, define the semantic layer as an explicit root `[semantic]` schema with nested summary structs/arrays, then attach it with `[semantic(ViewType)]` and populate it with `emit(...)` / `emit_row(...)` / `emit_node(...)`.
4. After the raw structure is honest, prefer declarative semantic views for user-facing summaries and navigation. Use `dynamic_array` and `dynamic_struct` only for byte-backed referenced data that should appear in the raw tree, and `dynamic_container` only for niche mapped-placement buckets such as PE sections. Do not recommend compiled/native semantic hooks for new authoring unless there is no declarative route.
5. When adding or renaming Strata keywords, update all keyword-facing surfaces in the same change: `src/causeway/strata/README.md`, `scripts/qtcreator/q22-strata.xml`, parser/lexer tests, and any shipped `.strata` examples that demonstrate the syntax.
6. Add or update focused tests in `tests/structview_tests.cpp` for every behavior change that affects rendering.
7. Validate with `/tmp/q22-structview-build/tests/structview_tests` when available, and build `hexedit` if embedded Strata resources or renderer code changed.

## Authoring Rules

- Keep `.strata` definitions honest: do not hide or reorder raw fields to make the tree prettier.
- Use `enum(Name)` for one-of-N values and `bitflag(Name)` for independent masks.
- Do not use `bitflag(...)` for packed fields whose values overlap, such as PE section alignment bits. Leave those raw or add a purpose-built renderer later.
- Leave architecture-specific fields raw unless the definition can switch safely on architecture. ELF `e_flags` is intentionally raw because architectures assign different meanings to the same bits.
- When a variable array may exceed the display cap, pair `count(...)` with `extent(...)` so parent layout advances by the true byte length.
- On arrays, keep layout bounds on the array declaration and put per-element behavior inside `element(...)`: `[count(n), element(name(id), dynamic_array(...), emit_node(...))] Entry entries[];`. Direct `name(...)`, dynamic, semantic emit, diagnostic, navigation, or display tags on an array declaration are authoring errors.
- Use `dynamic_struct(...)` only on struct/union declarations, or inside `element(...)` for arrays of struct/union elements. Do not attach `dynamic_struct(...)` to scalar offset fields; put it on the owning record and reference the scalar field from `offset(...)`.
- `dynamic_array(...)`, `dynamic_struct(...)`, and `dynamic_container(...)` are non-layout Structure View attachments. They never change the owning field/struct/array element's consumed byte length, rendered extent, alignment, or following-field offset; use `extent(...)`, `count(...)`, `align(...)`, or `pad_to(...)` for actual layout.
- For sentinel-bounded arrays, prefer `max_count(...)` plus `terminated_by(...)` over using `count(...)` as an artificial cap. `terminated_by(...)` may be a scalar value, a byte sequence such as `{ 0, 0, 1 }`, or an expression over the rendered element's fields. String-like arrays hide terminators by default; struct/scalar arrays show them unless `terminator("hidden")` is specified.
- For ZIP-like formats, show both local records and index/trailer records when both exist; use top-level offset sorting in the renderer when physical order matters.
- For endian-sensitive formats, put `endian(...)` high enough for nested fields to inherit it.
- For named address spaces, define `offset_map("name", ...)` where the format defines that address space, then use `offset("name", expr)` mainly from dynamic or semantic rows. Avoid making referenced bytes look like inline raw fields. Use `value_at(...)` only for small one-off scalar probes in expressions.
- For the semantic layer, prefer one explicit root `[semantic]` schema that contains the destination structure for the summary tree; it renders as a top-level sibling of the raw root. Nest child structs/arrays inside that schema instead of scattering separate semantic roots.
- In `dynamic_array(...)` / `dynamic_struct(...)`, prefer `offset("name", expr)` for a named address space when the generated row should stay under the owning row. Keep `mapper(offset_map)` with anonymous maps when the generated row should attach to mapped dynamic containers, such as PE section buckets. Remember that this controls attachment/navigability only, not physical layout.
- For discriminators inside union candidates, use the shared-candidate field fallback when the field is declared identically by every case; otherwise use `select_offset(byteOffset)`.
- For exported root file associations, put every extension in one comma-separated `assoc(...)` tag, e.g. `assoc(".mp4", ".mov")`; do not repeat separate `assoc(...)` tags for the same export.

## Common Patterns

Flexible payload with true layout extent:

```c
[count(CompressedSize), extent(CompressedSize)]
byte CompressedData[];
```

Expandable bit masks:

```c
enum FLAGS { FLAG_A = 0x01, FLAG_B = 0x02 };
[bitflag(FLAGS)] word Flags;
```

Offset table based on a trailer:

```c
[offset(find_last({ 'P', 'K', 0x05, 0x06 }, 65557))]
ZIP_END_OF_CENTRAL_DIRECTORY_RECORD eocd;

[offset(eocd.OffsetOfStartOfCentralDirectory), count(eocd.TotalEntries)]
ZIP_CENTRAL_DIRECTORY_FILE_HEADER centralDirectory[];
```

Named array elements:

```c
[count(sectionCount), element(name(Name))]
SECTION_HEADER sections[];
```

## Validation

Use these checks when relevant:

```bash
cmake --build /tmp/q22-structview-build --target structview_tests
/tmp/q22-structview-build/tests/structview_tests
cmake --build /tmp/q22-structview-build --target hexedit
```

If the configured build tree is stale, configure a fresh build under `/tmp`. If CMake needs network for dependencies, request approval rather than working around the sandbox.
