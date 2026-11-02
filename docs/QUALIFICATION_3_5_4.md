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
