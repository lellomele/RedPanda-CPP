[Italiano](VERIFICHE_3_5_4.md) | [English](QUALIFICATION_3_5_4.md)

# 3.5.4 foundation checkpoint qualification

Date: 2 November 2026. Status: development, `alpha.1`, on
`stabilization/3.5.4`. This is a checkpoint of the structural migration, not a
stable release qualification. See the [architecture plan](ARCHITECTURE_3_5_4.md).
The previous working installation and published packages have not been replaced.

## Configuration and results

Windows x64, Qt 6.11.2, GCC 16.1.0, Ninja, Release build; portable configuration,
Lua, VCS and SDCC support enabled. Deprecated Qt declarations are compilation
errors. NASM and SDCC execution, other operating systems and Qt 5 are not
qualified by this checkpoint.

| Verification | Result |
|---|---|
| CTest | 37/37 suites passed, 60.74 seconds |
| Native Windows UI, English | 1,617 checks passed, none skipped |
| Native Windows UI, Italian | 1,966 checks passed, none skipped |
| Real external tools | GCC, Make, GDB and an offline Git repository exercised |
| Source consistency | 2,672 repository files identical in two qualification captures |

The SHA-256 of the identical manifests captured during and at the end of qualification is
`7181ba8c4e72c0c68f5eb6f0e74b5bb27a60ec5a0423ff9bb9846b3e1cae3134`.
Documentation was updated after these checks; production sources were not.
GUI checks include parameterized theme/contrast checks and are not a count of
distinct features or a measure of code coverage.

The first restricted GUI run could not use the native Recycle Bin and its test
driver did not dismiss the resulting error dialog. The driver now records the
error and continues with a failed assertion. The successful native Windows run
used temporary profiles and bounded test processes; no skip concealed the error.

## Exercised contracts

- Complete document capture during typing, multiline replacement/paste,
  Undo/Redo, empty buffers, load/reload failure, rename, Save As and reopening;
  capture during a mutation is explicitly refused.
- Real recovery files follow every document commit; failed replacement retains
  the last confirmed file, and retry writes the latest complete content.
- Project operations preserve source/destination buffers and unit options on
  cancellation or failure, including closed files and case-only Windows rename.
- External rename conflicts preserve distinct units/buffers, survive metadata
  save/load, and prevent a compiler worker from being submitted until resolved.
- Close No/Cancel preserves dirty buffers; changed membership/revisions reject
  stale decisions. Project close retires its planned editors even when a unit
  association is missing.
- Semantic builders terminate on cumulative work/resource limits, keep detached
  value results and identify partial output. Lifecycle tests exercise stale and
  duplicate receipts, cancellation, deadlines and prevention of repeated commit.

## Editing measurements

QSynEdit-only benchmark, five samples per size; median milliseconds. The editing
column measures the complete batch of 40 type/backspace pairs.

| Lines | Load C++ document | 40 editing pairs | UTF-8 file load | Atomic file save |
|---|---:|---:|---:|---:|
| 3,000 | 5.60 | 9.70 | 7.82 | 3.58 |
| 30,000 | 58.01 | 27.96 | 83.18 | 18.82 |
| 100,000 | 181.12 | 76.26 | 292.20 | 52.46 |

These measurements do not include the full IDE/parser/completion workload and
do not establish a speedup over the previous release.

## Remaining release gates

Publish dependency-validated immutable semantic results; migrate Structure,
completion, hints and refactoring consumers; bound total retained snapshot/cache
memory; reconcile project document identity and Save As; expose relocation repair
through a controller that preserves unit settings. Replace live compilation
configuration/project reads with immutable requests and integrate asynchronous
service shutdown. Qualify stress/performance workloads and exact release artifacts
after those runtime migrations. The earlier 4–5 GiB incident remains unexplained
by direct evidence; this test pass is not a claim to have reproduced its cause.

## Second integration: 3 November, still in development

The regular suite passed **45/45 CTest groups in 95.00 seconds**, including the
new registered document-file workflow (26 checks), document identity/snapshot
workflow (24 checks), frozen compilation and process-tree tests. Save As write
failure/retry and encoding-observer tests now exercise the production controller;
standalone widget tests verify that unregistered Save As cannot write files.
The missing save-error notification was restored on failed controller operations.
This run does not certify the separate red persistence probes below.

The subsequent native Windows UI run **did not qualify**: English and Italian
compiler workflows exposed the temporary autolink adapter's dependency on the
editor parser. Completion disabled or analysis still busy can prevent a build.
The 22 isolated compiler-request checks passed with autolink disabled; they did
not cover this integration. An independent, frozen dependency scan owned by the
registered compiler worker is required. Project build/error/rebuild and GDB
checks passed in this run; they do not certify immutable project preparation.

Additional bounded persistence probes confirmed:

- A post-save observer can edit the buffer after disk commit; legacy Save then
  returns true and clears the dirty flag even though disk and buffer differ.
- A pre-save observer can close the editor, followed by a native access violation
  (`0xC0000005`). A separate modal-error probe observed execution resuming after
  editor destruction without crashing in that execution.

These are blocking findings for 3.5.4. Unify ordinary Save, Save As and encoding
conversion around captured document identity/revision, explicit commit outcomes
and safe notification/lifetime boundaries; add permanent regression coverage.
Current development packages have not replaced the user's working installation.

Read provenance now covers unsaved included headers, closed-file changes even
with restored timestamps, newly available headers, missing versus empty files,
directory changes and registered relative-document roots. End verification uses
the remaining source-memory admission rather than a second full independent cap.
This proves observed source versions, not an atomic filesystem snapshot, current
GUI intent or complete semantic freshness. Mutable configuration, the runtime
publisher/consumers, aggregate input-generation admission and global shutdown
remain open. The union publisher and retirement scheduler remain isolated kernels.

Italian resources contain 101 additional controller/compiler/document messages.
A separate audit identified 53 helper message sources still requiring structured
localization at their UI boundary. Those are not claimed translated by the TS
update. No new whole-IDE performance improvement is claimed by these checks.

## Independent autolink and shared document persistence

Autolink for standalone C and C++ builds now reads saved source dependencies
inside the registered compiler worker. The request captures the compiler and
rule values on the GUI thread. The worker discovers toolchain paths and macros,
reads a bounded disk-only dependency graph and checks the source versions it
observed. Editing assistance can be disabled or busy while a build proceeds.
This scanner uses IDE include semantics. The actual compiler still controls
preprocessing and compilation, and the source files remain externally mutable
between the two stages.

Save, Save As and encoding conversion share the same controller and serializer.
A write uses the document's stable destination and captured revision, with
atomic file replacement. A change or close before writing invalidates the
operation. An edit after commit remains dirty, and the receipt identifies the
revision written to disk. The controller continues to distinguish text and
project-metadata commits for Save As. The timestamped backup-copy path remains
separate from saving the current document.

The legacy probes also reproduced a retarget during the pre-save notification
that overwrote the newly selected fixture file. The regression suite now
exercises that path through the registered editor and verifies both disk files.
The development build passed 47 of 48 CTest groups in 166.14 seconds. The
registered document-file workflow passed all 32 checks, including failed
conversion, post-commit edits, Undo/Redo at the committed revision, pre-commit
retargeting and synchronous close requests. The parser tests also passed the
raw-string, macro-include, conditional and scan-limit regressions. When an
include cannot be resolved by the IDE scanner, the build log reports that
autolink was skipped for that invocation and GCC produces its own diagnostic.

The native Windows run passed 1,618 checks in English and 1,967 in Italian,
with no skips. The only CTest failure was a separate offscreen runner check.
The expected marker was produced, but completion was observed at 10.13 seconds
against an eight-second limit. The native run observed 0.61 and 0.74 seconds.
The exact source of the intermittent delay is still under investigation.

The parser configuration queue passed ten isolated cases. Integration with the
live IDE and migration of Structure to immutable data remain separate gates.
This build used Qt 6.11.2 on Windows x64 and retains the alpha version suffix.
