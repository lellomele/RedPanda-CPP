[Italiano](PIANO_3_5_4.md) | [English](ARCHITECTURE_3_5_4.md)

# 3.5.4 structural migration

Status: implementation in progress. Target: Windows / Qt 6.11.2. The production
installation is not updated by development builds.

## Decision and invariants

Use a modular monolith with single ownership of mutable state, message-based
work submission and immutable, versioned snapshots. Preserve QSynEdit and the
existing parser's language behavior while changing their integration contracts.

1. The GUI owns documents and active-pane context. Workers never enumerate or
   read QWidget objects, mutable settings or mutable project configuration.
2. An analysis request owns an immutable view of open document contents. A
   result identifies the document, revision, configuration generation and job.
3. The analysis engine has one writer. Scheduling, cancellation, cache reset
   and configuration changes are explicit operations, not incidental effects
   of focus. Semantic consumers receive immutable data or bounded queries.
4. Application operations return an explicit outcome. A cancellation before a
   resource's commit does not modify it. An already committed save is not
   silently rolled back because a subsequent component fails.
5. Dialogs belong to controllers and do not hold parser leases. No GUI event
   waits indefinitely for a worker or pumps events while waiting for a lock.
6. Every service is registered for shutdown independently of selected widgets.
   Documents and controllers remain alive until services acknowledge stopping.
7. Persistence validates candidates and checks writes/commits. Resource budgets
   are checked before building large payloads, not only after serialization.
8. Remove replaced legacy entry points when a vertical migration is complete.
   An adapter is temporary and has a stated removal condition.

## Work packages and exit conditions

| Package | Deliverable | Exit condition |
|---|---|---|
| P0 | Reproducible checkpoint of the previously tested local changes | Source and restore point preserved on `stabilization/3.5.4` |
| P1 | Operation outcomes; safe project operations, close decisions and persistence | Cancellation/failure tests preserve buffers, files, dirty state and associations |
| P2 | GUI-owned DocumentStore and immutable input snapshots | No parser worker callback traverses EditorManager or QWidget |
| P3 | Analysis coordination, cancellation and result snapshots | Explicit transitions; no raw mutable model exposure to migrated consumers |
| P4 | Structure/completion/hints/refactoring consumers | Correct revision and active context; one Undo transaction for changes |
| P5 | Immutable compile requests and coordinated service lifecycle | Configuration consistency and complete, bounded shutdown |
| P6 | Qualification and release identity | Functional/failure/stress checks and measured editing workloads tied to exact source/toolchain |

Dependencies: P0 precedes all changes. P1 data protection and P2 input ownership
can proceed in parallel under these contracts; P3/P4 build on P2. P5 uses the
same request/outcome/lifecycle contracts. Qualification is continuous, with a
final gate after legacy removal.

## Verification strategy

Keep deterministic regression tests for the audited failures: project rename
of a closed file, target-save cancellation, failed metadata writes, multi-file
close No/Cancel, parser work during tab changes, split shutdown with distinct
parsers, configuration changes during compile, and faults during persistence.
Add controllable scheduling and immutable-source tests before replacing input
or result ownership. Use generated fixtures and isolated profiles, bounded
processes and timeouts. Existing 28 passing CTest suites are a starting point,
not evidence for yet-unimplemented contracts.

Track latency, memory and cancellation for the complete IDE as well as the
existing QSynEdit benchmark. Do not claim a performance gain without measuring
the relevant workload, or explain the previous 4–5 GiB episode without evidence.

## Remaining decisions

Worker-process isolation can be considered after the in-process contracts are
sound. No complete editor rewrite or parser replacement is implied. Release
promotion requires resolving or explicitly mitigating audited P1 issues and
recording remaining operational limits. This document is updated with completed
packages and removed adapters as implementation proceeds.

## Baseline and first integration

The tested pre-migration local changes are preserved in commit `3898f098`, on
`stabilization/3.5.4`, with a separate working-tree archive. The first integration
implements project operation outcomes/atomic metadata, staged close decisions,
owner-thread Project disposal, recovery preflight, atomic color/backup commits
and immutable parser input views. The foundation checkpoint passed 37 CTest
suites and native Windows UI checks in English (1,617) and Italian (1,966),
including real GCC/Make/GDB and offline Git workflows, with no skipped checks.
These results qualify this checkpoint's exercised paths, not the full migration.

The subsequent contract review also covers document mutation/rename boundaries,
recovery cache invalidation, cancelled project removal, case-only Windows rename
and durable external-rename conflicts. Compilation refuses unresolved relocation
state before submitting a worker. See the [qualification record](QUALIFICATION_3_5_4.md).

P2 input snapshots do not yet guarantee a fresh semantic index: invalidating
cached include dependencies by revision and publishing immutable semantic results
remain P3 work. Compiler requests and asynchronous application shutdown remain
P5 work. The owner event loop must stay alive until background services release
their objects; posting deleteLater is not a substitute for that lifecycle gate.

The semantic builder, compilation value types/checked generated-file writer and
lifecycle kernel have integrated unit tests but do not yet replace their legacy
runtime adapters. Before semantic publication, add a whole-cache byte budget and
shared ownership of cross-file subgraphs; a per-file traversal limit is not a
total application memory limit. Selection keys are best-effort lookup keys, not
unique symbol identities. Project document binding and Save As require explicit
identity-based reconciliation; the relocation repair API still needs its GUI
controller. These are remaining work, not completed guarantees.

Temporary adapter: EditorClosePlan currently compares GUI text snapshots at
close time. Replace that identity/revision check with DocumentStore revisions
once all mutation notifications have been qualified. The scope/membership and
discard decisions remain part of the controller contract.
