---
name: q22-strata-authoring
description: Write, review, and debug q22 Strata `.strata`/legacy `.struct` binary format definitions. Use when Codex is asked to add or fix files under `src/causeway/strata/`, define binary layouts, add enum/bitflag display tags, model offset-based tables, update Strata docs, or troubleshoot Structure View rendering of PE, ELF, ZIP, or new file formats.
---

# Q22 Strata Authoring

## Workflow

1. Read `AGENTS.md` and `src/causeway/strata/README.md` before changing `.strata` files.
2. Prefer a pure structure model first: fields in physical file order, `offset(...)` for tables elsewhere, `count(...)` for exact-count arrays, `max_count(...)`/`terminated_by(...)` for sentinel-bounded arrays, and `extent(...)` when rendered children are capped but layout must advance by full byte length.
3. Use `dynamic_array`, `dynamic_struct`, `dynamic_container`, and semantic `view(...)` only when the raw structure cannot express the relationship cleanly, such as PE RVA-mapped data directories.
4. When adding or renaming Strata keywords, update all keyword-facing surfaces in the same change: `src/causeway/strata/README.md`, `scripts/qtcreator/q22-strata.xml`, parser/lexer tests, and any shipped `.strata` examples that demonstrate the syntax.
5. Add or update focused tests in `tests/structview_tests.cpp` for every behavior change that affects rendering.
6. Validate with `/tmp/q22-structview-build/tests/structview_tests` when available, and build `hexedit` if embedded Strata resources or renderer code changed.

## Authoring Rules

- Keep `.strata` definitions honest: do not hide or reorder raw fields to make the tree prettier.
- Use `enum(Name)` for one-of-N values and `bitflag(Name)` for independent masks.
- Do not use `bitflag(...)` for packed fields whose values overlap, such as PE section alignment bits. Leave those raw or add a purpose-built renderer later.
- Leave architecture-specific fields raw unless the definition can switch safely on architecture. ELF `e_flags` is intentionally raw because architectures assign different meanings to the same bits.
- When a variable array may exceed the display cap, pair `count(...)` with `extent(...)` so parent layout advances by the true byte length.
- For sentinel-bounded arrays, prefer `max_count(...)` plus `terminated_by(...)` over using `count(...)` as an artificial cap. `terminated_by(...)` may be a scalar value, a byte sequence such as `{ 0, 0, 1 }`, or an expression over the rendered element's fields. String-like arrays hide terminators by default; struct/scalar arrays show them unless `terminator("hidden")` is specified.
- For ZIP-like formats, show both local records and index/trailer records when both exist; use top-level offset sorting in the renderer when physical order matters.
- For endian-sensitive formats, put `endian(...)` high enough for nested fields to inherit it.
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

## Validation

Use these checks when relevant:

```bash
cmake --build /tmp/q22-structview-build --target structview_tests
/tmp/q22-structview-build/tests/structview_tests
cmake --build /tmp/q22-structview-build --target hexedit
```

If the configured build tree is stale, configure a fresh build under `/tmp`. If CMake needs network for dependencies, request approval rather than working around the sandbox.
