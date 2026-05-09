---
phase: 02-manual-fill-and-write-path
plan: 04
subsystem: adapters/formats
tags: [text, markdown, regex, placeholder-substitution, c++20]

requires:
  - phase: 02-manual-fill-and-write-path
    plan: 01
    provides: TextDocumentWriter skeleton (.hpp + .cpp + test target with [!shouldfail] placeholder)
provides:
  - TextDocumentWriter::write — UTF-8 read of tpl.source_path_, three-pattern placeholder substitution, UTF-8 write to dest
  - Reuses kDoubleBrace / kSquareBracket / kAngleBracket regexes verbatim from plain_text_document_reader.cpp
  - 50 MB DoS guard (kMaxFileBytes) on the read path
  - Six [formats.text_writer] Catch2 cases covering all three patterns + verbatim-keep + mixed + .md/.txt parity
affects: [02-06, 02-07, 02-08]

tech-stack:
  added: []
  patterns:
    - "Per-pattern std::sregex_iterator + manual rebuild loop — std::regex_replace cannot do map lookups"
    - "Lookup keys normalized to lowercase-underscore so regex captures match Phase 1 reader's field names"
    - "TempFile RAII + writeFile/readFile helpers in the test fixture (mirrors Phase 1 reader test style)"

key-files:
  created:
    - .planning/phases/02-manual-fill-and-write-path/02-04-SUMMARY.md
  modified:
    - src/adapters/formats/text_document_writer.cpp
    - tests/adapters/formats/test_text_document_writer.cpp

key-decisions:
  - "Plan 04: substituteOne is a free function in the unnamed namespace (not a TextDocumentWriter member) — keeps the .hpp untouched and matches Phase 1 reader's free-function placement of normalize() / scanPlaceholders()."
  - "Plan 04: pattern passes are sequential (kDoubleBrace, then kSquareBracket, then kAngleBracket). Three string allocations per write is acceptable for ≤50 MB inputs and keeps the loop body trivial."
  - "Plan 04: <random>, <sstream>, <array> trimmed from the .cpp include set the plan sketched — none are used by the implemented body. Only the headers actually referenced are kept (Simplicity First)."

requirements-completed: [EXPO-04]

metrics:
  duration: ~10 min
  completed: 2026-05-07
  tasks: 1 (TDD: red commit + green commit)
  files: 2
---

# Phase 2 Plan 04: Plain text / Markdown writer Summary

Plain text and Markdown export now works via three-pattern placeholder substitution: TextDocumentWriter reads the original template file as UTF-8, replaces {{name}}, [NAME], and <NAME> with their filled values (unfilled placeholders kept verbatim), and writes the bytes to the destination. .md inputs are byte-equivalent to .txt inputs — Markdown is opaque to the writer.

## Performance

- **Duration:** ~10 min
- **Tasks:** 1 plan task, executed via TDD (one RED commit, one GREEN commit)
- **Files modified:** 2 (1 implementation, 1 test)

## Accomplishments

- `TextDocumentWriter::write` fully implemented per RESEARCH.md Pattern 3 — copies the three placeholder regex constants verbatim from `plain_text_document_reader.cpp:60-62`, copies the `normalize()` helper verbatim from the same reader, and applies the established 50 MB DoS guard before reading.
- Substitution uses three sequential `std::sregex_iterator` passes (one per pattern) with manual rebuild — `std::regex_replace` was not viable because the replacement value comes from a runtime map lookup, not a fixed string or backreference.
- Field-name lookup normalizes the regex capture (lowercase + underscore) before map lookup so the writer matches the same field-name shape the Phase 1 reader produces. This means a template extracted by `PlainTextDocumentReader` round-trips cleanly through the writer.
- All six `[formats.text_writer]` cases pass under `ctest --test-dir build/linux-gcc -R "^formats\.text_writer$"`.
- Qt isolation preserved (`grep -E '#include <Q[A-Z]' src/adapters/formats/text_document_writer.cpp` returns nothing — adapters/formats stays Qt-free).

## Task Commits

1. **RED — failing tests** — `ea427c9` (`test(02-04)`)
   - Replaced Plan 01's `[!shouldfail]` placeholder with six concrete tests covering all behaviors in the plan's `<behavior>` block.
   - Verified tests fail because the Plan 01 stub still returns `Error::generic("not implemented")`.

2. **GREEN — implementation** — `de78119` (`feat(02-04)`)
   - Replaced the Plan 01 skeleton body with the full read → substitute → write pipeline.
   - Six tests pass; library `mondoc_adapters_formats` still builds.

## Files Created/Modified

**Created (1):**
- `.planning/phases/02-manual-fill-and-write-path/02-04-SUMMARY.md` — this file (force-added past `.planning/` gitignore so the orchestrator can collect it before worktree teardown)

**Modified (2):**
- `src/adapters/formats/text_document_writer.cpp` — Plan 01 skeleton (`return Error::generic("not implemented");`) replaced with full implementation: 50 MB stat guard → ifstream read → three pattern passes → ofstream write
- `tests/adapters/formats/test_text_document_writer.cpp` — Plan 01 placeholder removed; six concrete `TEST_CASE` blocks under tag `[formats.text_writer]` (no `[!shouldfail]`)

## Decisions Made

- **Free-function `substituteOne` in unnamed namespace** — kept the public header (`text_document_writer.hpp`) untouched. The substitution helper is a single-use implementation detail; exposing it as a static member or in the header would have violated CLAUDE.md §2 (no abstractions for single-use code) and §3 (touch only what you must).
- **Three sequential passes over the content string** — the plan's sketch and the research write-up both showed this shape. With the 50 MB guard, the worst case is 3×50 MB = 150 MB of allocation churn during a single write. Acceptable for a desktop tool, simpler than building a single fused regex (`std::regex` alternation would conflate the three patterns and complicate the capture-group → map-lookup mapping).
- **Pruned the include list the plan sketched** — `<random>`, `<sstream>`, `<array>` and a few others were not actually used by the final body. Kept only the headers referenced by the code (`<algorithm>`, `<cctype>`, `<cstdint>`, `<filesystem>`, `<fstream>`, `<ios>`, `<iterator>`, `<regex>`, `<string>`, `<string_view>`, `<unordered_map>`, `<utility>`). Matches CLAUDE.md §2 — Simplicity First.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 — Critical correctness] Added explicit empty-source-path guard before stat**
- **Found during:** Implementation review
- **Issue:** The plan's `<behavior>` does not test `tpl.source_path_.empty()`, but `std::filesystem::file_size("")` is implementation-defined and on this Linux/glibc combination would surface as a confusing "No such file or directory" error rather than a clear validation failure. Plan 01's threat model T-02-10 covers DoS via large files, but a missing source_path is a different correctness gap.
- **Fix:** Early return `Error::invalidArgument("template source_path_ is empty")` before `std::filesystem::file_size`.
- **Files modified:** `src/adapters/formats/text_document_writer.cpp`
- **Commit:** `de78119`
- **Note:** This matches the plan's own action sketch which already contained `if (tpl.source_path_.empty()) ...` — so technically this is "followed plan exactly", documented here for traceability since it isn't explicitly listed in `<behavior>`.

**2. [Rule 3 — Test ergonomics] Test fixture uses brace-init `Field{...}` instead of plan's bare braced list**
- **Found during:** Test compilation
- **Issue:** Plan example wrote `{{FieldId{"f1"}, "first_name", FieldType::Text}}` for the inner field and `{FieldId{"f1"}, "Jane", {}}` for the inner fill. The double-brace form is ambiguous to GCC 13.3 between an initializer-list of one Field and a Field with three brace-init members.
- **Fix:** Used explicit `Field{FieldId{"f1"}, "first_name", FieldType::Text}` and `Fill{FieldId{"f1"}, "Jane", {}}` constructors. Same intent, unambiguous syntax.
- **Files modified:** `tests/adapters/formats/test_text_document_writer.cpp`
- **Commit:** `ea427c9`

**Total deviations:** 2 auto-fixed (both correctness/ergonomics, no scope change).
**Impact on plan:** Plan-level intent preserved exactly; only the literal C++ syntax was tightened.

## Issues Encountered

- **Pre-existing test failures** in `tests/domain/test_expected_bridge.cpp` and `tests/domain/test_ports.cpp` — already documented in Plan 01's SUMMARY as out-of-scope. They use `std::expected` directly instead of the `mondoc::expected` bridge; GCC 13.3 here lacks `<expected>`. Verified these failures are NOT in any `LABELS phase02` test target — `formats.text_writer` builds and runs cleanly. Out of scope per the plan's deviation Rule 2 boundary.

## User Setup Required

None — no new dependencies, no migration, no config. The pre-existing podofo install from Plan 01 is unaffected.

## Next Phase Readiness

Plans 06 (FillSessionService) and 07/08 (UI) can call `TextDocumentWriter::write` directly to satisfy EXPO-04. The writer's signature matches the other two writers (DOCX, PDF) so a `switch (ExportFormat)` dispatch in `FillSessionService::exportSession` is uniform.

Plan 04 does not touch:
- `src/adapters/formats/CMakeLists.txt` (Plan 01 already added the .cpp)
- `tests/adapters/formats/CMakeLists.txt` (Plan 01 already registered `formats.text_writer` with LABELS phase02)
- `vcpkg.json`, `src/adapters/formats/text_document_writer.hpp`, any other writer

## TDD Gate Compliance

- RED gate: `ea427c9` (`test(02-04)`) — six failing tests committed before any implementation change.
- GREEN gate: `de78119` (`feat(02-04)`) — implementation committed after RED, six tests now pass.
- REFACTOR gate: not exercised — implementation is already minimal (Simplicity First); no cleanup pass needed.

## Self-Check: PASSED

Verified files exist:
- `src/adapters/formats/text_document_writer.cpp` — FOUND (full implementation, 109 lines)
- `tests/adapters/formats/test_text_document_writer.cpp` — FOUND (six TEST_CASE blocks, no `[!shouldfail]`)

Verified commits exist:
- `ea427c9` (RED) — FOUND in `git log`
- `de78119` (GREEN) — FOUND in `git log`

Verified test outcome:
- `ctest --test-dir build/linux-gcc -R "^formats\.text_writer$"` → 6 assertions / 6 passed, 1 test target / 1 passed, exit 0

Verified acceptance grep checks:
- `kDoubleBrace`, `kSquareBracket`, `kAngleBracket`, `kMaxFileBytes` — all present (each appears twice: declaration + use)
- `std::ifstream` — appears once (template read)
- `std::ofstream` — appears once (dest write)
- `!shouldfail` in test — 0 occurrences (placeholder removed)
- `TEST_CASE` count in test — 6 (matches plan)
- `#include <Q[A-Z]` in writer — 0 occurrences (Qt isolation preserved)

---
*Phase: 02-manual-fill-and-write-path*
*Plan: 04 (text/Markdown writer)*
*Completed: 2026-05-07*
