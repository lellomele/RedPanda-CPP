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

The second integration binds project units to session DocumentIds and moves Save
As and Relink into one GUI-owned controller. Plans validate identity/revision and
unit settings across modal operations. Text and project metadata have separate
commit receipts; a partial commit preserves the current buffer and is explicit.
Native Windows path comparison preserves Unicode filenames that Qt case folding
would merge. Ambiguous case-sensitive namespaces are refused without overwriting
units or documents. Unverifiable virtual aliases remain conflicts.

Analysis requests now capture the working directory and decoder candidates as
values. The worker records actual source versions and negative include probes,
checks resource admission before decoding/materializing lines, and verifies
observed end versions. Registered document roots retain their original absolute
paths through both preliminary dependency analysis and parsing. A changed cache
dependency triggers a conservative asynchronous rebuild. This read certificate
does not certify the still-legacy mutable configuration or current GUI intent;
immutable configuration, publication and consumers remain P3/P4 work.

Single-file and stdin compilation now use frozen configuration/input values.
Diagnostic hints check the accepted DocumentId/revision, including open headers;
original messages remain in the panel. Windows process trees join a native Job
atomically at launch; completion requires a kernel acknowledgement that all
descendants have exited. Legacy Project/SDCCProject preparation and global
asynchronous shutdown still need migration. Non-Windows does not claim this
Windows process-tree guarantee.

The union/shard semantic representation, aggregate allocation ledger and one
retirement worker have isolated tests but are not the runtime publisher yet.
Retained objects stay charged until actual reclamation. The ledger is not a
process RSS limit and does not bound the mutable engine or all accepted source
snapshot generations. Selection hints can be ambiguous; they are not identities.

New persistence audit probes confirmed two blocking legacy Save paths: an edit
from a post-save observer can be marked clean without being written, and closing
the editor from a pre-save observer can cause an access violation. SaveExisting,
Save As and encoding conversion must follow one persistence contract before
release. Passing the regular suite does not remove these separate red probes.
The owner event loop must also remain alive until every service acknowledges
stopping; deleteLater and an empty task queue are not shutdown certificates.

Temporary adapter: EditorClosePlan currently compares GUI text snapshots at
close time. Replace that identity/revision check with DocumentStore revisions
once all mutation notifications have been qualified. The scope/membership and
discard decisions remain part of the controller contract.

## Runtime Structure publication

Structure now consumes the immutable projection described above. Its GUI
controller tracks the current workspace and document and submits projection
requests to the parser writer. The tree model owns no parser objects and
expansion reads only published rows. Publication checks the certified parse job
and current document identity. A complete project view requires every requested
project root. Retained snapshots stay charged until background reclamation.
Completion, refactoring and the remaining service shutdown paths are separate
migration steps. The qualification record contains the Windows results.
